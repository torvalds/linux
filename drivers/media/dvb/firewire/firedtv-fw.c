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
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <asm/page.h>
#include <asm/system.h>

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

int fdtv_lock(struct firedtv *fdtv, u64 addr, void *data)
{
	return node_req(fdtv, addr, data, 8, TCODE_LOCK_COMPARE_SWAP);
}

int fdtv_read(struct firedtv *fdtv, u64 addr, void *data)
{
	return node_req(fdtv, addr, data, 4, TCODE_READ_QUADLET_REQUEST);
}

int fdtv_write(struct firedtv *fdtv, u64 addr, void *data, size_t len)
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

struct fdtv_ir_context {
	struct fw_iso_context *context;
	struct fw_iso_buffer buffer;
	int interrupt_packet;
	int current_packet;
	char *pages[N_PAGES];
};

static int queue_iso(struct fdtv_ir_context *ctx, int index)
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
	struct fdtv_ir_context *ctx = fdtv->ir_context;
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

int fdtv_start_iso(struct firedtv *fdtv)
{
	struct fdtv_ir_context *ctx;
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

	fdtv->ir_context = ctx;

	return 0;
fail:
	fw_iso_buffer_destroy(&ctx->buffer, device->card);
fail_context_destroy:
	fw_iso_context_destroy(ctx->context);
fail_free:
	kfree(ctx);

	return err;
}

void fdtv_stop_iso(struct firedtv *fdtv)
{
	struct fdtv_ir_context *ctx = fdtv->ir_context;

	fw_iso_context_stop(ctx->context);
	fw_iso_buffer_destroy(&ctx->buffer, device_of(fdtv)->card);
	fw_iso_context_destroy(ctx->context);
	kfree(ctx);
}

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

static const char * const model_names[] = {
	[FIREDTV_UNKNOWN] = "unknown type",
	[FIREDTV_DVB_S]   = "FireDTV S/CI",
	[FIREDTV_DVB_C]   = "FireDTV C/CI",
	[FIREDTV_DVB_T]   = "FireDTV T/CI",
	[FIREDTV_DVB_S2]  = "FireDTV S2  ",
};

/* Adjust the template string if models with longer names appear. */
#define MAX_MODEL_NAME_LEN sizeof("FireDTV ????")

static int node_probe(struct device *dev)
{
	struct firedtv *fdtv;
	char name[MAX_MODEL_NAME_LEN];
	int name_len, i, err;

	fdtv = kzalloc(sizeof(*fdtv), GFP_KERNEL);
	if (!fdtv)
		return -ENOMEM;

	dev_set_drvdata(dev, fdtv);
	fdtv->device		= dev;
	fdtv->isochannel	= -1;
	fdtv->voltage		= 0xff;
	fdtv->tone		= 0xff;

	mutex_init(&fdtv->avc_mutex);
	init_waitqueue_head(&fdtv->avc_wait);
	mutex_init(&fdtv->demux_mutex);
	INIT_WORK(&fdtv->remote_ctrl_work, avc_remote_ctrl_work);

	name_len = fw_csr_string(fw_unit(dev)->directory, CSR_MODEL,
				 name, sizeof(name));
	for (i = ARRAY_SIZE(model_names); --i; )
		if (strlen(model_names[i]) <= name_len &&
		    strncmp(name, model_names[i], name_len) == 0)
			break;
	fdtv->type = i;

	err = fdtv_register_rc(fdtv, dev);
	if (err)
		goto fail_free;

	spin_lock_irq(&node_list_lock);
	list_add_tail(&fdtv->list, &node_list);
	spin_unlock_irq(&node_list_lock);

	err = avc_identify_subunit(fdtv);
	if (err)
		goto fail;

	err = fdtv_dvb_register(fdtv, model_names[fdtv->type]);
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

#define MATCH_FLAGS (IEEE1394_MATCH_VENDOR_ID | IEEE1394_MATCH_MODEL_ID | \
		     IEEE1394_MATCH_SPECIFIER_ID | IEEE1394_MATCH_VERSION)

#define DIGITAL_EVERYWHERE_OUI	0x001287
#define AVC_UNIT_SPEC_ID_ENTRY	0x00a02d
#define AVC_SW_VERSION_ENTRY	0x010001

static const struct ieee1394_device_id fdtv_id_table[] = {
	{
		/* FloppyDTV S/CI and FloppyDTV S2 */
		.match_flags	= MATCH_FLAGS,
		.vendor_id	= DIGITAL_EVERYWHERE_OUI,
		.model_id	= 0x000024,
		.specifier_id	= AVC_UNIT_SPEC_ID_ENTRY,
		.version	= AVC_SW_VERSION_ENTRY,
	}, {
		/* FloppyDTV T/CI */
		.match_flags	= MATCH_FLAGS,
		.vendor_id	= DIGITAL_EVERYWHERE_OUI,
		.model_id	= 0x000025,
		.specifier_id	= AVC_UNIT_SPEC_ID_ENTRY,
		.version	= AVC_SW_VERSION_ENTRY,
	}, {
		/* FloppyDTV C/CI */
		.match_flags	= MATCH_FLAGS,
		.vendor_id	= DIGITAL_EVERYWHERE_OUI,
		.model_id	= 0x000026,
		.specifier_id	= AVC_UNIT_SPEC_ID_ENTRY,
		.version	= AVC_SW_VERSION_ENTRY,
	}, {
		/* FireDTV S/CI and FloppyDTV S2 */
		.match_flags	= MATCH_FLAGS,
		.vendor_id	= DIGITAL_EVERYWHERE_OUI,
		.model_id	= 0x000034,
		.specifier_id	= AVC_UNIT_SPEC_ID_ENTRY,
		.version	= AVC_SW_VERSION_ENTRY,
	}, {
		/* FireDTV T/CI */
		.match_flags	= MATCH_FLAGS,
		.vendor_id	= DIGITAL_EVERYWHERE_OUI,
		.model_id	= 0x000035,
		.specifier_id	= AVC_UNIT_SPEC_ID_ENTRY,
		.version	= AVC_SW_VERSION_ENTRY,
	}, {
		/* FireDTV C/CI */
		.match_flags	= MATCH_FLAGS,
		.vendor_id	= DIGITAL_EVERYWHERE_OUI,
		.model_id	= 0x000036,
		.specifier_id	= AVC_UNIT_SPEC_ID_ENTRY,
		.version	= AVC_SW_VERSION_ENTRY,
	}, {}
};
MODULE_DEVICE_TABLE(ieee1394, fdtv_id_table);

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

static int __init fdtv_init(void)
{
	int ret;

	ret = fw_core_add_address_handler(&fcp_handler, &fcp_region);
	if (ret < 0)
		return ret;

	ret = driver_register(&fdtv_driver.driver);
	if (ret < 0)
		fw_core_remove_address_handler(&fcp_handler);

	return ret;
}

static void __exit fdtv_exit(void)
{
	driver_unregister(&fdtv_driver.driver);
	fw_core_remove_address_handler(&fcp_handler);
}

module_init(fdtv_init);
module_exit(fdtv_exit);

MODULE_AUTHOR("Andreas Monitzer <andy@monitzer.com>");
MODULE_AUTHOR("Ben Backx <ben@bbackx.com>");
MODULE_DESCRIPTION("FireDTV DVB Driver");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("FireDTV DVB");
