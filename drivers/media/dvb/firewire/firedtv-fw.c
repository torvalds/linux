/*
 * FireDTV driver -- firewire I/O backend
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/page.h>

#include <dvb_demux.h>

#include "firedtv.h"

static LIST_HEAD(node_list);
static DEFINE_SPINLOCK(node_list_lock);

static inline struct fw_device *device_of(struct firedtv *fdtv)
{
	return fw_device(fdtv->device->parent);
}

static int node_req(struct firedtv *fdtv, u64 addr, void *data, size_t len,
		    int tcode)
{
	struct fw_device *device = device_of(fdtv);
	int rcode, generation = device->generation;

	smp_rmb(); /* node_id vs. generation */

	rcode = fw_run_transaction(device->card, tcode, device->node_id,
			generation, device->max_speed, addr, data, len);

	return rcode != RCODE_COMPLETE ? -EIO : 0;
}

static int node_lock(struct firedtv *fdtv, u64 addr, void *data)
{
	return node_req(fdtv, addr, data, 8, TCODE_LOCK_COMPARE_SWAP);
}

static int node_read(struct firedtv *fdtv, u64 addr, void *data)
{
	return node_req(fdtv, addr, data, 4, TCODE_READ_QUADLET_REQUEST);
}

static int node_write(struct firedtv *fdtv, u64 addr, void *data, size_t len)
{
	return node_req(fdtv, addr, data, len, TCODE_WRITE_BLOCK_REQUEST);
}

#define ISO_HEADER_SIZE			4
#define CIP_HEADER_SIZE			8
#define MPEG2_TS_HEADER_SIZE		4
#define MPEG2_TS_SOURCE_PACKET_SIZE	(4 + 188)

#define MAX_PACKET_SIZE		1024  /* 776, rounded up to 2^n */
#define PACKETS_PER_PAGE	(PAGE_SIZE / MAX_PACKET_SIZE)
#define N_PACKETS		64    /* buffer size */
#define N_PAGES			DIV_ROUND_UP(N_PACKETS, PACKETS_PER_PAGE)
#define IRQ_INTERVAL		16

struct firedtv_receive_context {
	struct fw_iso_context *context;
	struct fw_iso_buffer buffer;
	int interrupt_packet;
	int current_packet;
	char *pages[N_PAGES];
};

static int queue_iso(struct firedtv_receive_context *ctx, int index)
{
	struct fw_iso_packet p;

	p.payload_length = MAX_PACKET_SIZE;
	p.interrupt = !(++ctx->interrupt_packet & (IRQ_INTERVAL - 1));
	p.skip = 0;
	p.header_length = ISO_HEADER_SIZE;

	return fw_iso_context_queue(ctx->context, &p, &ctx->buffer,
				    index * MAX_PACKET_SIZE);
}

static void handle_iso(struct fw_iso_context *context, u32 cycle,
		       size_t header_length, void *header, void *data)
{
	struct firedtv *fdtv = data;
	struct firedtv_receive_context *ctx = fdtv->backend_data;
	__be32 *h, *h_end;
	int length, err, i = ctx->current_packet;
	char *p, *p_end;

	for (h = header, h_end = h + header_length / 4; h < h_end; h++) {
		length = be32_to_cpup(h) >> 16;
		if (unlikely(length > MAX_PACKET_SIZE)) {
			dev_err(fdtv->device, "length = %d\n", length);
			length = MAX_PACKET_SIZE;
		}

		p = ctx->pages[i / PACKETS_PER_PAGE]
				+ (i % PACKETS_PER_PAGE) * MAX_PACKET_SIZE;
		p_end = p + length;

		for (p += CIP_HEADER_SIZE + MPEG2_TS_HEADER_SIZE; p < p_end;
		     p += MPEG2_TS_SOURCE_PACKET_SIZE)
			dvb_dmx_swfilter_packets(&fdtv->demux, p, 1);

		err = queue_iso(ctx, i);
		if (unlikely(err))
			dev_err(fdtv->device, "requeue failed\n");

		i = (i + 1) & (N_PACKETS - 1);
	}
	ctx->current_packet = i;
}

static int start_iso(struct firedtv *fdtv)
{
	struct firedtv_receive_context *ctx;
	struct fw_device *device = device_of(fdtv);
	int i, err;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->context = fw_iso_context_create(device->card,
			FW_ISO_CONTEXT_RECEIVE, fdtv->isochannel,
			device->max_speed, ISO_HEADER_SIZE, handle_iso, fdtv);
	if (IS_ERR(ctx->context)) {
		err = PTR_ERR(ctx->context);
		goto fail_free;
	}

	err = fw_iso_buffer_init(&ctx->buffer, device->card,
				 N_PAGES, DMA_FROM_DEVICE);
	if (err)
		goto fail_context_destroy;

	ctx->interrupt_packet = 0;
	ctx->current_packet = 0;

	for (i = 0; i < N_PAGES; i++)
		ctx->pages[i] = page_address(ctx->buffer.pages[i]);

	for (i = 0; i < N_PACKETS; i++) {
		err = queue_iso(ctx, i);
		if (err)
			goto fail;
	}

	err = fw_iso_context_start(ctx->context, -1, 0,
				   FW_ISO_CONTEXT_MATCH_ALL_TAGS);
	if (err)
		goto fail;

	fdtv->backend_data = ctx;

	return 0;
fail:
	fw_iso_buffer_destroy(&ctx->buffer, device->card);
fail_context_destroy:
	fw_iso_context_destroy(ctx->context);
fail_free:
	kfree(ctx);

	return err;
}

static void stop_iso(struct firedtv *fdtv)
{
	struct firedtv_receive_context *ctx = fdtv->backend_data;

	fw_iso_context_stop(ctx->context);
	fw_iso_buffer_destroy(&ctx->buffer, device_of(fdtv)->card);
	fw_iso_context_destroy(ctx->context);
	kfree(ctx);
}

static const struct firedtv_backend backend = {
	.lock		= node_lock,
	.read		= node_read,
	.write		= node_write,
	.start_iso	= start_iso,
	.stop_iso	= stop_iso,
};

static void handle_fcp(struct fw_card *card, struct fw_request *request,
		       int tcode, int destination, int source, int generation,
		       unsigned long long offset, void *payload, size_t length,
		       void *callback_data)
{
	struct firedtv *f, *fdtv = NULL;
	struct fw_device *device;
	unsigned long flags;
	int su;

	if (length < 2 || (((u8 *)payload)[0] & 0xf0) != 0)
		return;

	su = ((u8 *)payload)[1] & 0x7;

	spin_lock_irqsave(&node_list_lock, flags);
	list_for_each_entry(f, &node_list, list) {
		device = device_of(f);
		if (device->generation != generation)
			continue;

		smp_rmb(); /* node_id vs. generation */

		if (device->card == card &&
		    device->node_id == source &&
		    (f->subunit == su || (f->subunit == 0 && su == 0x7))) {
			fdtv = f;
			break;
		}
	}
	spin_unlock_irqrestore(&node_list_lock, flags);

	if (fdtv)
		avc_recv(fdtv, payload, length);
}

static struct fw_address_handler fcp_handler = {
	.length           = CSR_FCP_END - CSR_FCP_RESPONSE,
	.address_callback = handle_fcp,
};

static const struct fw_address_region fcp_region = {
	.start	= CSR_REGISTER_BASE + CSR_FCP_RESPONSE,
	.end	= CSR_REGISTER_BASE + CSR_FCP_END,
};

/* Adjust the template string if models with longer names appear. */
#define MAX_MODEL_NAME_LEN sizeof("FireDTV ????")

static int node_probe(struct device *dev)
{
	struct firedtv *fdtv;
	char name[MAX_MODEL_NAME_LEN];
	int name_len, err;

	name_len = fw_csr_string(fw_unit(dev)->directory, CSR_MODEL,
				 name, sizeof(name));

	fdtv = fdtv_alloc(dev, &backend, name, name_len >= 0 ? name_len : 0);
	if (!fdtv)
		return -ENOMEM;

	err = fdtv_register_rc(fdtv, dev);
	if (err)
		goto fail_free;

	spin_lock_irq(&node_list_lock);
	list_add_tail(&fdtv->list, &node_list);
	spin_unlock_irq(&node_list_lock);

	err = avc_identify_subunit(fdtv);
	if (err)
		goto fail;

	err = fdtv_dvb_register(fdtv);
	if (err)
		goto fail;

	avc_register_remote_control(fdtv);

	return 0;
fail:
	spin_lock_irq(&node_list_lock);
	list_del(&fdtv->list);
	spin_unlock_irq(&node_list_lock);
	fdtv_unregister_rc(fdtv);
fail_free:
	kfree(fdtv);

	return err;
}

static int node_remove(struct device *dev)
{
	struct firedtv *fdtv = dev_get_drvdata(dev);

	fdtv_dvb_unregister(fdtv);

	spin_lock_irq(&node_list_lock);
	list_del(&fdtv->list);
	spin_unlock_irq(&node_list_lock);

	fdtv_unregister_rc(fdtv);

	kfree(fdtv);
	return 0;
}

static void node_update(struct fw_unit *unit)
{
	struct firedtv *fdtv = dev_get_drvdata(&unit->device);

	if (fdtv->isochannel >= 0)
		cmp_establish_pp_connection(fdtv, fdtv->subunit,
					    fdtv->isochannel);
}

static struct fw_driver fdtv_driver = {
	.driver   = {
		.owner  = THIS_MODULE,
		.name   = "firedtv",
		.bus    = &fw_bus_type,
		.probe  = node_probe,
		.remove = node_remove,
	},
	.update   = node_update,
	.id_table = fdtv_id_table,
};

int __init fdtv_fw_init(void)
{
	int ret;

	ret = fw_core_add_address_handler(&fcp_handler, &fcp_region);
	if (ret < 0)
		return ret;

	return driver_register(&fdtv_driver.driver);
}

void fdtv_fw_exit(void)
{
	driver_unregister(&fdtv_driver.driver);
	fw_core_remove_address_handler(&fcp_handler);
}
