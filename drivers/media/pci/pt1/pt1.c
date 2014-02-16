/*
 * driver for Earthsoft PT1/PT2
 *
 * Copyright (C) 2009 HIRANO Takahito <hiranotaka@zng.info>
 *
 * based on pt1dvr - http://pt1dvr.sourceforge.jp/
 * 	by Tomoaki Ishikawa <tomy@users.sourceforge.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/ratelimit.h>

#include "dvbdev.h"
#include "dvb_demux.h"
#include "dmxdev.h"
#include "dvb_net.h"
#include "dvb_frontend.h"

#include "va1j5jf8007t.h"
#include "va1j5jf8007s.h"

#define DRIVER_NAME "earth-pt1"

#define PT1_PAGE_SHIFT 12
#define PT1_PAGE_SIZE (1 << PT1_PAGE_SHIFT)
#define PT1_NR_UPACKETS 1024
#define PT1_NR_BUFS 511

struct pt1_buffer_page {
	__le32 upackets[PT1_NR_UPACKETS];
};

struct pt1_table_page {
	__le32 next_pfn;
	__le32 buf_pfns[PT1_NR_BUFS];
};

struct pt1_buffer {
	struct pt1_buffer_page *page;
	dma_addr_t addr;
};

struct pt1_table {
	struct pt1_table_page *page;
	dma_addr_t addr;
	struct pt1_buffer bufs[PT1_NR_BUFS];
};

#define PT1_NR_ADAPS 4

struct pt1_adapter;

struct pt1 {
	struct pci_dev *pdev;
	void __iomem *regs;
	struct i2c_adapter i2c_adap;
	int i2c_running;
	struct pt1_adapter *adaps[PT1_NR_ADAPS];
	struct pt1_table *tables;
	struct task_struct *kthread;
	int table_index;
	int buf_index;

	struct mutex lock;
	int power;
	int reset;
};

struct pt1_adapter {
	struct pt1 *pt1;
	int index;

	u8 *buf;
	int upacket_count;
	int packet_count;
	int st_count;

	struct dvb_adapter adap;
	struct dvb_demux demux;
	int users;
	struct dmxdev dmxdev;
	struct dvb_frontend *fe;
	int (*orig_set_voltage)(struct dvb_frontend *fe,
				fe_sec_voltage_t voltage);
	int (*orig_sleep)(struct dvb_frontend *fe);
	int (*orig_init)(struct dvb_frontend *fe);

	fe_sec_voltage_t voltage;
	int sleep;
};

#define pt1_printk(level, pt1, format, arg...)	\
	dev_printk(level, &(pt1)->pdev->dev, format, ##arg)

static void pt1_write_reg(struct pt1 *pt1, int reg, u32 data)
{
	writel(data, pt1->regs + reg * 4);
}

static u32 pt1_read_reg(struct pt1 *pt1, int reg)
{
	return readl(pt1->regs + reg * 4);
}

static int pt1_nr_tables = 8;
module_param_named(nr_tables, pt1_nr_tables, int, 0);

static void pt1_increment_table_count(struct pt1 *pt1)
{
	pt1_write_reg(pt1, 0, 0x00000020);
}

static void pt1_init_table_count(struct pt1 *pt1)
{
	pt1_write_reg(pt1, 0, 0x00000010);
}

static void pt1_register_tables(struct pt1 *pt1, u32 first_pfn)
{
	pt1_write_reg(pt1, 5, first_pfn);
	pt1_write_reg(pt1, 0, 0x0c000040);
}

static void pt1_unregister_tables(struct pt1 *pt1)
{
	pt1_write_reg(pt1, 0, 0x08080000);
}

static int pt1_sync(struct pt1 *pt1)
{
	int i;
	for (i = 0; i < 57; i++) {
		if (pt1_read_reg(pt1, 0) & 0x20000000)
			return 0;
		pt1_write_reg(pt1, 0, 0x00000008);
	}
	pt1_printk(KERN_ERR, pt1, "could not sync\n");
	return -EIO;
}

static u64 pt1_identify(struct pt1 *pt1)
{
	int i;
	u64 id;
	id = 0;
	for (i = 0; i < 57; i++) {
		id |= (u64)(pt1_read_reg(pt1, 0) >> 30 & 1) << i;
		pt1_write_reg(pt1, 0, 0x00000008);
	}
	return id;
}

static int pt1_unlock(struct pt1 *pt1)
{
	int i;
	pt1_write_reg(pt1, 0, 0x00000008);
	for (i = 0; i < 3; i++) {
		if (pt1_read_reg(pt1, 0) & 0x80000000)
			return 0;
		schedule_timeout_uninterruptible((HZ + 999) / 1000);
	}
	pt1_printk(KERN_ERR, pt1, "could not unlock\n");
	return -EIO;
}

static int pt1_reset_pci(struct pt1 *pt1)
{
	int i;
	pt1_write_reg(pt1, 0, 0x01010000);
	pt1_write_reg(pt1, 0, 0x01000000);
	for (i = 0; i < 10; i++) {
		if (pt1_read_reg(pt1, 0) & 0x00000001)
			return 0;
		schedule_timeout_uninterruptible((HZ + 999) / 1000);
	}
	pt1_printk(KERN_ERR, pt1, "could not reset PCI\n");
	return -EIO;
}

static int pt1_reset_ram(struct pt1 *pt1)
{
	int i;
	pt1_write_reg(pt1, 0, 0x02020000);
	pt1_write_reg(pt1, 0, 0x02000000);
	for (i = 0; i < 10; i++) {
		if (pt1_read_reg(pt1, 0) & 0x00000002)
			return 0;
		schedule_timeout_uninterruptible((HZ + 999) / 1000);
	}
	pt1_printk(KERN_ERR, pt1, "could not reset RAM\n");
	return -EIO;
}

static int pt1_do_enable_ram(struct pt1 *pt1)
{
	int i, j;
	u32 status;
	status = pt1_read_reg(pt1, 0) & 0x00000004;
	pt1_write_reg(pt1, 0, 0x00000002);
	for (i = 0; i < 10; i++) {
		for (j = 0; j < 1024; j++) {
			if ((pt1_read_reg(pt1, 0) & 0x00000004) != status)
				return 0;
		}
		schedule_timeout_uninterruptible((HZ + 999) / 1000);
	}
	pt1_printk(KERN_ERR, pt1, "could not enable RAM\n");
	return -EIO;
}

static int pt1_enable_ram(struct pt1 *pt1)
{
	int i, ret;
	int phase;
	schedule_timeout_uninterruptible((HZ + 999) / 1000);
	phase = pt1->pdev->device == 0x211a ? 128 : 166;
	for (i = 0; i < phase; i++) {
		ret = pt1_do_enable_ram(pt1);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static void pt1_disable_ram(struct pt1 *pt1)
{
	pt1_write_reg(pt1, 0, 0x0b0b0000);
}

static void pt1_set_stream(struct pt1 *pt1, int index, int enabled)
{
	pt1_write_reg(pt1, 2, 1 << (index + 8) | enabled << index);
}

static void pt1_init_streams(struct pt1 *pt1)
{
	int i;
	for (i = 0; i < PT1_NR_ADAPS; i++)
		pt1_set_stream(pt1, i, 0);
}

static int pt1_filter(struct pt1 *pt1, struct pt1_buffer_page *page)
{
	u32 upacket;
	int i;
	int index;
	struct pt1_adapter *adap;
	int offset;
	u8 *buf;
	int sc;

	if (!page->upackets[PT1_NR_UPACKETS - 1])
		return 0;

	for (i = 0; i < PT1_NR_UPACKETS; i++) {
		upacket = le32_to_cpu(page->upackets[i]);
		index = (upacket >> 29) - 1;
		if (index < 0 || index >=  PT1_NR_ADAPS)
			continue;

		adap = pt1->adaps[index];
		if (upacket >> 25 & 1)
			adap->upacket_count = 0;
		else if (!adap->upacket_count)
			continue;

		if (upacket >> 24 & 1)
			printk_ratelimited(KERN_INFO "earth-pt1: device "
				"buffer overflowing. table[%d] buf[%d]\n",
				pt1->table_index, pt1->buf_index);
		sc = upacket >> 26 & 0x7;
		if (adap->st_count != -1 && sc != ((adap->st_count + 1) & 0x7))
			printk_ratelimited(KERN_INFO "earth-pt1: data loss"
				" in streamID(adapter)[%d]\n", index);
		adap->st_count = sc;

		buf = adap->buf;
		offset = adap->packet_count * 188 + adap->upacket_count * 3;
		buf[offset] = upacket >> 16;
		buf[offset + 1] = upacket >> 8;
		if (adap->upacket_count != 62)
			buf[offset + 2] = upacket;

		if (++adap->upacket_count >= 63) {
			adap->upacket_count = 0;
			if (++adap->packet_count >= 21) {
				dvb_dmx_swfilter_packets(&adap->demux, buf, 21);
				adap->packet_count = 0;
			}
		}
	}

	page->upackets[PT1_NR_UPACKETS - 1] = 0;
	return 1;
}

static int pt1_thread(void *data)
{
	struct pt1 *pt1;
	struct pt1_buffer_page *page;

	pt1 = data;
	set_freezable();

	while (!kthread_should_stop()) {
		try_to_freeze();

		page = pt1->tables[pt1->table_index].bufs[pt1->buf_index].page;
		if (!pt1_filter(pt1, page)) {
			schedule_timeout_interruptible((HZ + 999) / 1000);
			continue;
		}

		if (++pt1->buf_index >= PT1_NR_BUFS) {
			pt1_increment_table_count(pt1);
			pt1->buf_index = 0;
			if (++pt1->table_index >= pt1_nr_tables)
				pt1->table_index = 0;
		}
	}

	return 0;
}

static void pt1_free_page(struct pt1 *pt1, void *page, dma_addr_t addr)
{
	dma_free_coherent(&pt1->pdev->dev, PT1_PAGE_SIZE, page, addr);
}

static void *pt1_alloc_page(struct pt1 *pt1, dma_addr_t *addrp, u32 *pfnp)
{
	void *page;
	dma_addr_t addr;

	page = dma_alloc_coherent(&pt1->pdev->dev, PT1_PAGE_SIZE, &addr,
				  GFP_KERNEL);
	if (page == NULL)
		return NULL;

	BUG_ON(addr & (PT1_PAGE_SIZE - 1));
	BUG_ON(addr >> PT1_PAGE_SHIFT >> 31 >> 1);

	*addrp = addr;
	*pfnp = addr >> PT1_PAGE_SHIFT;
	return page;
}

static void pt1_cleanup_buffer(struct pt1 *pt1, struct pt1_buffer *buf)
{
	pt1_free_page(pt1, buf->page, buf->addr);
}

static int
pt1_init_buffer(struct pt1 *pt1, struct pt1_buffer *buf,  u32 *pfnp)
{
	struct pt1_buffer_page *page;
	dma_addr_t addr;

	page = pt1_alloc_page(pt1, &addr, pfnp);
	if (page == NULL)
		return -ENOMEM;

	page->upackets[PT1_NR_UPACKETS - 1] = 0;

	buf->page = page;
	buf->addr = addr;
	return 0;
}

static void pt1_cleanup_table(struct pt1 *pt1, struct pt1_table *table)
{
	int i;

	for (i = 0; i < PT1_NR_BUFS; i++)
		pt1_cleanup_buffer(pt1, &table->bufs[i]);

	pt1_free_page(pt1, table->page, table->addr);
}

static int
pt1_init_table(struct pt1 *pt1, struct pt1_table *table, u32 *pfnp)
{
	struct pt1_table_page *page;
	dma_addr_t addr;
	int i, ret;
	u32 buf_pfn;

	page = pt1_alloc_page(pt1, &addr, pfnp);
	if (page == NULL)
		return -ENOMEM;

	for (i = 0; i < PT1_NR_BUFS; i++) {
		ret = pt1_init_buffer(pt1, &table->bufs[i], &buf_pfn);
		if (ret < 0)
			goto err;

		page->buf_pfns[i] = cpu_to_le32(buf_pfn);
	}

	pt1_increment_table_count(pt1);
	table->page = page;
	table->addr = addr;
	return 0;

err:
	while (i--)
		pt1_cleanup_buffer(pt1, &table->bufs[i]);

	pt1_free_page(pt1, page, addr);
	return ret;
}

static void pt1_cleanup_tables(struct pt1 *pt1)
{
	struct pt1_table *tables;
	int i;

	tables = pt1->tables;
	pt1_unregister_tables(pt1);

	for (i = 0; i < pt1_nr_tables; i++)
		pt1_cleanup_table(pt1, &tables[i]);

	vfree(tables);
}

static int pt1_init_tables(struct pt1 *pt1)
{
	struct pt1_table *tables;
	int i, ret;
	u32 first_pfn, pfn;

	tables = vmalloc(sizeof(struct pt1_table) * pt1_nr_tables);
	if (tables == NULL)
		return -ENOMEM;

	pt1_init_table_count(pt1);

	i = 0;
	if (pt1_nr_tables) {
		ret = pt1_init_table(pt1, &tables[0], &first_pfn);
		if (ret)
			goto err;
		i++;
	}

	while (i < pt1_nr_tables) {
		ret = pt1_init_table(pt1, &tables[i], &pfn);
		if (ret)
			goto err;
		tables[i - 1].page->next_pfn = cpu_to_le32(pfn);
		i++;
	}

	tables[pt1_nr_tables - 1].page->next_pfn = cpu_to_le32(first_pfn);

	pt1_register_tables(pt1, first_pfn);
	pt1->tables = tables;
	return 0;

err:
	while (i--)
		pt1_cleanup_table(pt1, &tables[i]);

	vfree(tables);
	return ret;
}

static int pt1_start_polling(struct pt1 *pt1)
{
	int ret = 0;

	mutex_lock(&pt1->lock);
	if (!pt1->kthread) {
		pt1->kthread = kthread_run(pt1_thread, pt1, "earth-pt1");
		if (IS_ERR(pt1->kthread)) {
			ret = PTR_ERR(pt1->kthread);
			pt1->kthread = NULL;
		}
	}
	mutex_unlock(&pt1->lock);
	return ret;
}

static int pt1_start_feed(struct dvb_demux_feed *feed)
{
	struct pt1_adapter *adap;
	adap = container_of(feed->demux, struct pt1_adapter, demux);
	if (!adap->users++) {
		int ret;

		ret = pt1_start_polling(adap->pt1);
		if (ret)
			return ret;
		pt1_set_stream(adap->pt1, adap->index, 1);
	}
	return 0;
}

static void pt1_stop_polling(struct pt1 *pt1)
{
	int i, count;

	mutex_lock(&pt1->lock);
	for (i = 0, count = 0; i < PT1_NR_ADAPS; i++)
		count += pt1->adaps[i]->users;

	if (count == 0 && pt1->kthread) {
		kthread_stop(pt1->kthread);
		pt1->kthread = NULL;
	}
	mutex_unlock(&pt1->lock);
}

static int pt1_stop_feed(struct dvb_demux_feed *feed)
{
	struct pt1_adapter *adap;
	adap = container_of(feed->demux, struct pt1_adapter, demux);
	if (!--adap->users) {
		pt1_set_stream(adap->pt1, adap->index, 0);
		pt1_stop_polling(adap->pt1);
	}
	return 0;
}

static void
pt1_update_power(struct pt1 *pt1)
{
	int bits;
	int i;
	struct pt1_adapter *adap;
	static const int sleep_bits[] = {
		1 << 4,
		1 << 6 | 1 << 7,
		1 << 5,
		1 << 6 | 1 << 8,
	};

	bits = pt1->power | !pt1->reset << 3;
	mutex_lock(&pt1->lock);
	for (i = 0; i < PT1_NR_ADAPS; i++) {
		adap = pt1->adaps[i];
		switch (adap->voltage) {
		case SEC_VOLTAGE_13: /* actually 11V */
			bits |= 1 << 1;
			break;
		case SEC_VOLTAGE_18: /* actually 15V */
			bits |= 1 << 1 | 1 << 2;
			break;
		default:
			break;
		}

		/* XXX: The bits should be changed depending on adap->sleep. */
		bits |= sleep_bits[i];
	}
	pt1_write_reg(pt1, 1, bits);
	mutex_unlock(&pt1->lock);
}

static int pt1_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t voltage)
{
	struct pt1_adapter *adap;

	adap = container_of(fe->dvb, struct pt1_adapter, adap);
	adap->voltage = voltage;
	pt1_update_power(adap->pt1);

	if (adap->orig_set_voltage)
		return adap->orig_set_voltage(fe, voltage);
	else
		return 0;
}

static int pt1_sleep(struct dvb_frontend *fe)
{
	struct pt1_adapter *adap;

	adap = container_of(fe->dvb, struct pt1_adapter, adap);
	adap->sleep = 1;
	pt1_update_power(adap->pt1);

	if (adap->orig_sleep)
		return adap->orig_sleep(fe);
	else
		return 0;
}

static int pt1_wakeup(struct dvb_frontend *fe)
{
	struct pt1_adapter *adap;

	adap = container_of(fe->dvb, struct pt1_adapter, adap);
	adap->sleep = 0;
	pt1_update_power(adap->pt1);
	schedule_timeout_uninterruptible((HZ + 999) / 1000);

	if (adap->orig_init)
		return adap->orig_init(fe);
	else
		return 0;
}

static void pt1_free_adapter(struct pt1_adapter *adap)
{
	adap->demux.dmx.close(&adap->demux.dmx);
	dvb_dmxdev_release(&adap->dmxdev);
	dvb_dmx_release(&adap->demux);
	dvb_unregister_adapter(&adap->adap);
	free_page((unsigned long)adap->buf);
	kfree(adap);
}

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static struct pt1_adapter *
pt1_alloc_adapter(struct pt1 *pt1)
{
	struct pt1_adapter *adap;
	void *buf;
	struct dvb_adapter *dvb_adap;
	struct dvb_demux *demux;
	struct dmxdev *dmxdev;
	int ret;

	adap = kzalloc(sizeof(struct pt1_adapter), GFP_KERNEL);
	if (!adap) {
		ret = -ENOMEM;
		goto err;
	}

	adap->pt1 = pt1;

	adap->voltage = SEC_VOLTAGE_OFF;
	adap->sleep = 1;

	buf = (u8 *)__get_free_page(GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto err_kfree;
	}

	adap->buf = buf;
	adap->upacket_count = 0;
	adap->packet_count = 0;
	adap->st_count = -1;

	dvb_adap = &adap->adap;
	dvb_adap->priv = adap;
	ret = dvb_register_adapter(dvb_adap, DRIVER_NAME, THIS_MODULE,
				   &pt1->pdev->dev, adapter_nr);
	if (ret < 0)
		goto err_free_page;

	demux = &adap->demux;
	demux->dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING;
	demux->priv = adap;
	demux->feednum = 256;
	demux->filternum = 256;
	demux->start_feed = pt1_start_feed;
	demux->stop_feed = pt1_stop_feed;
	demux->write_to_decoder = NULL;
	ret = dvb_dmx_init(demux);
	if (ret < 0)
		goto err_unregister_adapter;

	dmxdev = &adap->dmxdev;
	dmxdev->filternum = 256;
	dmxdev->demux = &demux->dmx;
	dmxdev->capabilities = 0;
	ret = dvb_dmxdev_init(dmxdev, dvb_adap);
	if (ret < 0)
		goto err_dmx_release;

	return adap;

err_dmx_release:
	dvb_dmx_release(demux);
err_unregister_adapter:
	dvb_unregister_adapter(dvb_adap);
err_free_page:
	free_page((unsigned long)buf);
err_kfree:
	kfree(adap);
err:
	return ERR_PTR(ret);
}

static void pt1_cleanup_adapters(struct pt1 *pt1)
{
	int i;
	for (i = 0; i < PT1_NR_ADAPS; i++)
		pt1_free_adapter(pt1->adaps[i]);
}

static int pt1_init_adapters(struct pt1 *pt1)
{
	int i;
	struct pt1_adapter *adap;
	int ret;

	for (i = 0; i < PT1_NR_ADAPS; i++) {
		adap = pt1_alloc_adapter(pt1);
		if (IS_ERR(adap)) {
			ret = PTR_ERR(adap);
			goto err;
		}

		adap->index = i;
		pt1->adaps[i] = adap;
	}
	return 0;

err:
	while (i--)
		pt1_free_adapter(pt1->adaps[i]);

	return ret;
}

static void pt1_cleanup_frontend(struct pt1_adapter *adap)
{
	dvb_unregister_frontend(adap->fe);
}

static int pt1_init_frontend(struct pt1_adapter *adap, struct dvb_frontend *fe)
{
	int ret;

	adap->orig_set_voltage = fe->ops.set_voltage;
	adap->orig_sleep = fe->ops.sleep;
	adap->orig_init = fe->ops.init;
	fe->ops.set_voltage = pt1_set_voltage;
	fe->ops.sleep = pt1_sleep;
	fe->ops.init = pt1_wakeup;

	ret = dvb_register_frontend(&adap->adap, fe);
	if (ret < 0)
		return ret;

	adap->fe = fe;
	return 0;
}

static void pt1_cleanup_frontends(struct pt1 *pt1)
{
	int i;
	for (i = 0; i < PT1_NR_ADAPS; i++)
		pt1_cleanup_frontend(pt1->adaps[i]);
}

struct pt1_config {
	struct va1j5jf8007s_config va1j5jf8007s_config;
	struct va1j5jf8007t_config va1j5jf8007t_config;
};

static const struct pt1_config pt1_configs[2] = {
	{
		{
			.demod_address = 0x1b,
			.frequency = VA1J5JF8007S_20MHZ,
		},
		{
			.demod_address = 0x1a,
			.frequency = VA1J5JF8007T_20MHZ,
		},
	}, {
		{
			.demod_address = 0x19,
			.frequency = VA1J5JF8007S_20MHZ,
		},
		{
			.demod_address = 0x18,
			.frequency = VA1J5JF8007T_20MHZ,
		},
	},
};

static const struct pt1_config pt2_configs[2] = {
	{
		{
			.demod_address = 0x1b,
			.frequency = VA1J5JF8007S_25MHZ,
		},
		{
			.demod_address = 0x1a,
			.frequency = VA1J5JF8007T_25MHZ,
		},
	}, {
		{
			.demod_address = 0x19,
			.frequency = VA1J5JF8007S_25MHZ,
		},
		{
			.demod_address = 0x18,
			.frequency = VA1J5JF8007T_25MHZ,
		},
	},
};

static int pt1_init_frontends(struct pt1 *pt1)
{
	int i, j;
	struct i2c_adapter *i2c_adap;
	const struct pt1_config *configs, *config;
	struct dvb_frontend *fe[4];
	int ret;

	i = 0;
	j = 0;

	i2c_adap = &pt1->i2c_adap;
	configs = pt1->pdev->device == 0x211a ? pt1_configs : pt2_configs;
	do {
		config = &configs[i / 2];

		fe[i] = va1j5jf8007s_attach(&config->va1j5jf8007s_config,
					    i2c_adap);
		if (!fe[i]) {
			ret = -ENODEV; /* This does not sound nice... */
			goto err;
		}
		i++;

		fe[i] = va1j5jf8007t_attach(&config->va1j5jf8007t_config,
					    i2c_adap);
		if (!fe[i]) {
			ret = -ENODEV;
			goto err;
		}
		i++;

		ret = va1j5jf8007s_prepare(fe[i - 2]);
		if (ret < 0)
			goto err;

		ret = va1j5jf8007t_prepare(fe[i - 1]);
		if (ret < 0)
			goto err;

	} while (i < 4);

	do {
		ret = pt1_init_frontend(pt1->adaps[j], fe[j]);
		if (ret < 0)
			goto err;
	} while (++j < 4);

	return 0;

err:
	while (i-- > j)
		fe[i]->ops.release(fe[i]);

	while (j--)
		dvb_unregister_frontend(fe[j]);

	return ret;
}

static void pt1_i2c_emit(struct pt1 *pt1, int addr, int busy, int read_enable,
			 int clock, int data, int next_addr)
{
	pt1_write_reg(pt1, 4, addr << 18 | busy << 13 | read_enable << 12 |
		      !clock << 11 | !data << 10 | next_addr);
}

static void pt1_i2c_write_bit(struct pt1 *pt1, int addr, int *addrp, int data)
{
	pt1_i2c_emit(pt1, addr,     1, 0, 0, data, addr + 1);
	pt1_i2c_emit(pt1, addr + 1, 1, 0, 1, data, addr + 2);
	pt1_i2c_emit(pt1, addr + 2, 1, 0, 0, data, addr + 3);
	*addrp = addr + 3;
}

static void pt1_i2c_read_bit(struct pt1 *pt1, int addr, int *addrp)
{
	pt1_i2c_emit(pt1, addr,     1, 0, 0, 1, addr + 1);
	pt1_i2c_emit(pt1, addr + 1, 1, 0, 1, 1, addr + 2);
	pt1_i2c_emit(pt1, addr + 2, 1, 1, 1, 1, addr + 3);
	pt1_i2c_emit(pt1, addr + 3, 1, 0, 0, 1, addr + 4);
	*addrp = addr + 4;
}

static void pt1_i2c_write_byte(struct pt1 *pt1, int addr, int *addrp, int data)
{
	int i;
	for (i = 0; i < 8; i++)
		pt1_i2c_write_bit(pt1, addr, &addr, data >> (7 - i) & 1);
	pt1_i2c_write_bit(pt1, addr, &addr, 1);
	*addrp = addr;
}

static void pt1_i2c_read_byte(struct pt1 *pt1, int addr, int *addrp, int last)
{
	int i;
	for (i = 0; i < 8; i++)
		pt1_i2c_read_bit(pt1, addr, &addr);
	pt1_i2c_write_bit(pt1, addr, &addr, last);
	*addrp = addr;
}

static void pt1_i2c_prepare(struct pt1 *pt1, int addr, int *addrp)
{
	pt1_i2c_emit(pt1, addr,     1, 0, 1, 1, addr + 1);
	pt1_i2c_emit(pt1, addr + 1, 1, 0, 1, 0, addr + 2);
	pt1_i2c_emit(pt1, addr + 2, 1, 0, 0, 0, addr + 3);
	*addrp = addr + 3;
}

static void
pt1_i2c_write_msg(struct pt1 *pt1, int addr, int *addrp, struct i2c_msg *msg)
{
	int i;
	pt1_i2c_prepare(pt1, addr, &addr);
	pt1_i2c_write_byte(pt1, addr, &addr, msg->addr << 1);
	for (i = 0; i < msg->len; i++)
		pt1_i2c_write_byte(pt1, addr, &addr, msg->buf[i]);
	*addrp = addr;
}

static void
pt1_i2c_read_msg(struct pt1 *pt1, int addr, int *addrp, struct i2c_msg *msg)
{
	int i;
	pt1_i2c_prepare(pt1, addr, &addr);
	pt1_i2c_write_byte(pt1, addr, &addr, msg->addr << 1 | 1);
	for (i = 0; i < msg->len; i++)
		pt1_i2c_read_byte(pt1, addr, &addr, i == msg->len - 1);
	*addrp = addr;
}

static int pt1_i2c_end(struct pt1 *pt1, int addr)
{
	pt1_i2c_emit(pt1, addr,     1, 0, 0, 0, addr + 1);
	pt1_i2c_emit(pt1, addr + 1, 1, 0, 1, 0, addr + 2);
	pt1_i2c_emit(pt1, addr + 2, 1, 0, 1, 1, 0);

	pt1_write_reg(pt1, 0, 0x00000004);
	do {
		if (signal_pending(current))
			return -EINTR;
		schedule_timeout_interruptible((HZ + 999) / 1000);
	} while (pt1_read_reg(pt1, 0) & 0x00000080);
	return 0;
}

static void pt1_i2c_begin(struct pt1 *pt1, int *addrp)
{
	int addr;
	addr = 0;

	pt1_i2c_emit(pt1, addr,     0, 0, 1, 1, addr /* itself */);
	addr = addr + 1;

	if (!pt1->i2c_running) {
		pt1_i2c_emit(pt1, addr,     1, 0, 1, 1, addr + 1);
		pt1_i2c_emit(pt1, addr + 1, 1, 0, 1, 0, addr + 2);
		addr = addr + 2;
		pt1->i2c_running = 1;
	}
	*addrp = addr;
}

static int pt1_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct pt1 *pt1;
	int i;
	struct i2c_msg *msg, *next_msg;
	int addr, ret;
	u16 len;
	u32 word;

	pt1 = i2c_get_adapdata(adap);

	for (i = 0; i < num; i++) {
		msg = &msgs[i];
		if (msg->flags & I2C_M_RD)
			return -ENOTSUPP;

		if (i + 1 < num)
			next_msg = &msgs[i + 1];
		else
			next_msg = NULL;

		if (next_msg && next_msg->flags & I2C_M_RD) {
			i++;

			len = next_msg->len;
			if (len > 4)
				return -ENOTSUPP;

			pt1_i2c_begin(pt1, &addr);
			pt1_i2c_write_msg(pt1, addr, &addr, msg);
			pt1_i2c_read_msg(pt1, addr, &addr, next_msg);
			ret = pt1_i2c_end(pt1, addr);
			if (ret < 0)
				return ret;

			word = pt1_read_reg(pt1, 2);
			while (len--) {
				next_msg->buf[len] = word;
				word >>= 8;
			}
		} else {
			pt1_i2c_begin(pt1, &addr);
			pt1_i2c_write_msg(pt1, addr, &addr, msg);
			ret = pt1_i2c_end(pt1, addr);
			if (ret < 0)
				return ret;
		}
	}

	return num;
}

static u32 pt1_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm pt1_i2c_algo = {
	.master_xfer = pt1_i2c_xfer,
	.functionality = pt1_i2c_func,
};

static void pt1_i2c_wait(struct pt1 *pt1)
{
	int i;
	for (i = 0; i < 128; i++)
		pt1_i2c_emit(pt1, 0, 0, 0, 1, 1, 0);
}

static void pt1_i2c_init(struct pt1 *pt1)
{
	int i;
	for (i = 0; i < 1024; i++)
		pt1_i2c_emit(pt1, i, 0, 0, 1, 1, 0);
}

static void pt1_remove(struct pci_dev *pdev)
{
	struct pt1 *pt1;
	void __iomem *regs;

	pt1 = pci_get_drvdata(pdev);
	regs = pt1->regs;

	if (pt1->kthread)
		kthread_stop(pt1->kthread);
	pt1_cleanup_tables(pt1);
	pt1_cleanup_frontends(pt1);
	pt1_disable_ram(pt1);
	pt1->power = 0;
	pt1->reset = 1;
	pt1_update_power(pt1);
	pt1_cleanup_adapters(pt1);
	i2c_del_adapter(&pt1->i2c_adap);
	kfree(pt1);
	pci_iounmap(pdev, regs);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static int pt1_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int ret;
	void __iomem *regs;
	struct pt1 *pt1;
	struct i2c_adapter *i2c_adap;

	ret = pci_enable_device(pdev);
	if (ret < 0)
		goto err;

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret < 0)
		goto err_pci_disable_device;

	pci_set_master(pdev);

	ret = pci_request_regions(pdev, DRIVER_NAME);
	if (ret < 0)
		goto err_pci_disable_device;

	regs = pci_iomap(pdev, 0, 0);
	if (!regs) {
		ret = -EIO;
		goto err_pci_release_regions;
	}

	pt1 = kzalloc(sizeof(struct pt1), GFP_KERNEL);
	if (!pt1) {
		ret = -ENOMEM;
		goto err_pci_iounmap;
	}

	mutex_init(&pt1->lock);
	pt1->pdev = pdev;
	pt1->regs = regs;
	pci_set_drvdata(pdev, pt1);

	ret = pt1_init_adapters(pt1);
	if (ret < 0)
		goto err_kfree;

	mutex_init(&pt1->lock);

	pt1->power = 0;
	pt1->reset = 1;
	pt1_update_power(pt1);

	i2c_adap = &pt1->i2c_adap;
	i2c_adap->algo = &pt1_i2c_algo;
	i2c_adap->algo_data = NULL;
	i2c_adap->dev.parent = &pdev->dev;
	strcpy(i2c_adap->name, DRIVER_NAME);
	i2c_set_adapdata(i2c_adap, pt1);
	ret = i2c_add_adapter(i2c_adap);
	if (ret < 0)
		goto err_pt1_cleanup_adapters;

	pt1_i2c_init(pt1);
	pt1_i2c_wait(pt1);

	ret = pt1_sync(pt1);
	if (ret < 0)
		goto err_i2c_del_adapter;

	pt1_identify(pt1);

	ret = pt1_unlock(pt1);
	if (ret < 0)
		goto err_i2c_del_adapter;

	ret = pt1_reset_pci(pt1);
	if (ret < 0)
		goto err_i2c_del_adapter;

	ret = pt1_reset_ram(pt1);
	if (ret < 0)
		goto err_i2c_del_adapter;

	ret = pt1_enable_ram(pt1);
	if (ret < 0)
		goto err_i2c_del_adapter;

	pt1_init_streams(pt1);

	pt1->power = 1;
	pt1_update_power(pt1);
	schedule_timeout_uninterruptible((HZ + 49) / 50);

	pt1->reset = 0;
	pt1_update_power(pt1);
	schedule_timeout_uninterruptible((HZ + 999) / 1000);

	ret = pt1_init_frontends(pt1);
	if (ret < 0)
		goto err_pt1_disable_ram;

	ret = pt1_init_tables(pt1);
	if (ret < 0)
		goto err_pt1_cleanup_frontends;

	return 0;

err_pt1_cleanup_frontends:
	pt1_cleanup_frontends(pt1);
err_pt1_disable_ram:
	pt1_disable_ram(pt1);
	pt1->power = 0;
	pt1->reset = 1;
	pt1_update_power(pt1);
err_i2c_del_adapter:
	i2c_del_adapter(i2c_adap);
err_pt1_cleanup_adapters:
	pt1_cleanup_adapters(pt1);
err_kfree:
	kfree(pt1);
err_pci_iounmap:
	pci_iounmap(pdev, regs);
err_pci_release_regions:
	pci_release_regions(pdev);
err_pci_disable_device:
	pci_disable_device(pdev);
err:
	return ret;

}

static struct pci_device_id pt1_id_table[] = {
	{ PCI_DEVICE(0x10ee, 0x211a) },
	{ PCI_DEVICE(0x10ee, 0x222a) },
	{ },
};
MODULE_DEVICE_TABLE(pci, pt1_id_table);

static struct pci_driver pt1_driver = {
	.name		= DRIVER_NAME,
	.probe		= pt1_probe,
	.remove		= pt1_remove,
	.id_table	= pt1_id_table,
};

module_pci_driver(pt1_driver);

MODULE_AUTHOR("Takahito HIRANO <hiranotaka@zng.info>");
MODULE_DESCRIPTION("Earthsoft PT1/PT2 Driver");
MODULE_LICENSE("GPL");
