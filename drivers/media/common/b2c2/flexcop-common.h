/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux driver for digital TV devices equipped with B2C2 FlexcopII(b)/III
 * flexcop-common.h - common header file for device-specific source files
 * see flexcop.c for copyright information
 */
#ifndef __FLEXCOP_COMMON_H__
#define __FLEXCOP_COMMON_H__

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/mutex.h>

#include "flexcop-reg.h"

#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_net.h"
#include "dvb_frontend.h"

#define FC_MAX_FEED 256

#ifndef FC_LOG_PREFIX
#warning please define a log prefix for your file, using a default one
#define FC_LOG_PREFIX "b2c2-undef"
#endif

/* Steal from usb.h */
#undef err
#define err(format, arg...) \
	printk(KERN_ERR FC_LOG_PREFIX ": " format "\n" , ## arg)
#undef info
#define info(format, arg...) \
	printk(KERN_INFO FC_LOG_PREFIX ": " format "\n" , ## arg)
#undef warn
#define warn(format, arg...) \
	printk(KERN_WARNING FC_LOG_PREFIX ": " format "\n" , ## arg)

struct flexcop_dma {
	struct pci_dev *pdev;

	u8 *cpu_addr0;
	dma_addr_t dma_addr0;
	u8 *cpu_addr1;
	dma_addr_t dma_addr1;
	u32 size; /* size of each address in bytes */
};

struct flexcop_i2c_adapter {
	struct flexcop_device *fc;
	struct i2c_adapter i2c_adap;

	u8 no_base_addr;
	flexcop_i2c_port_t port;
};

/* Control structure for data definitions that are common to
 * the B2C2-based PCI and USB devices.
 */
struct flexcop_device {
	/* general */
	struct device *dev; /* for firmware_class */

#define FC_STATE_DVB_INIT 0x01
#define FC_STATE_I2C_INIT 0x02
#define FC_STATE_FE_INIT  0x04
	int init_state;

	/* device information */
	int has_32_hw_pid_filter;
	flexcop_revision_t rev;
	flexcop_device_type_t dev_type;
	flexcop_bus_t bus_type;

	/* dvb stuff */
	struct dvb_adapter dvb_adapter;
	struct dvb_frontend *fe;
	struct dvb_net dvbnet;
	struct dvb_demux demux;
	struct dmxdev dmxdev;
	struct dmx_frontend hw_frontend;
	struct dmx_frontend mem_frontend;
	int (*fe_sleep) (struct dvb_frontend *);

	struct flexcop_i2c_adapter fc_i2c_adap[3];
	struct mutex i2c_mutex;
	struct module *owner;

	/* options and status */
	int extra_feedcount;
	int feedcount;
	int pid_filtering;
	int fullts_streaming_state;
	int skip_6_hw_pid_filter;

	/* bus specific callbacks */
	flexcop_ibi_value(*read_ibi_reg) (struct flexcop_device *,
			flexcop_ibi_register);
	int (*write_ibi_reg) (struct flexcop_device *,
			flexcop_ibi_register, flexcop_ibi_value);
	int (*i2c_request) (struct flexcop_i2c_adapter *,
		flexcop_access_op_t, u8 chipaddr, u8 addr, u8 *buf, u16 len);
	int (*stream_control) (struct flexcop_device *, int);
	int (*get_mac_addr) (struct flexcop_device *fc, int extended);
	void *bus_specific;
};

/* exported prototypes */

/* from flexcop.c */
void flexcop_pass_dmx_data(struct flexcop_device *fc, u8 *buf, u32 len);
void flexcop_pass_dmx_packets(struct flexcop_device *fc, u8 *buf, u32 no);

struct flexcop_device *flexcop_device_kmalloc(size_t bus_specific_len);
void flexcop_device_kfree(struct flexcop_device *);

int flexcop_device_initialize(struct flexcop_device *);
void flexcop_device_exit(struct flexcop_device *fc);
void flexcop_reset_block_300(struct flexcop_device *fc);

/* from flexcop-dma.c */
int flexcop_dma_allocate(struct pci_dev *pdev,
		struct flexcop_dma *dma, u32 size);
void flexcop_dma_free(struct flexcop_dma *dma);

int flexcop_dma_control_timer_irq(struct flexcop_device *fc,
		flexcop_dma_index_t no, int onoff);
int flexcop_dma_control_size_irq(struct flexcop_device *fc,
		flexcop_dma_index_t no, int onoff);
int flexcop_dma_config(struct flexcop_device *fc, struct flexcop_dma *dma,
		flexcop_dma_index_t dma_idx);
int flexcop_dma_xfer_control(struct flexcop_device *fc,
		flexcop_dma_index_t dma_idx, flexcop_dma_addr_index_t index,
		int onoff);
int flexcop_dma_config_timer(struct flexcop_device *fc,
		flexcop_dma_index_t dma_idx, u8 cycles);

/* from flexcop-eeprom.c */
/* the PCI part uses this call to get the MAC address, the USB part has its own */
int flexcop_eeprom_check_mac_addr(struct flexcop_device *fc, int extended);

/* from flexcop-i2c.c */
/* the PCI part uses this a i2c_request callback, whereas the usb part has its own
 * one. We have it in flexcop-i2c.c, because it is going via the actual
 * I2C-channel of the flexcop.
 */
int flexcop_i2c_request(struct flexcop_i2c_adapter*, flexcop_access_op_t,
	u8 chipaddr, u8 addr, u8 *buf, u16 len);

/* from flexcop-sram.c */
int flexcop_sram_set_dest(struct flexcop_device *fc, flexcop_sram_dest_t dest,
	flexcop_sram_dest_target_t target);
void flexcop_wan_set_speed(struct flexcop_device *fc, flexcop_wan_speed_t s);
void flexcop_sram_ctrl(struct flexcop_device *fc,
		int usb_wan, int sramdma, int maximumfill);

/* global prototypes for the flexcop-chip */
/* from flexcop-fe-tuner.c */
int flexcop_frontend_init(struct flexcop_device *fc);
void flexcop_frontend_exit(struct flexcop_device *fc);

/* from flexcop-i2c.c */
int flexcop_i2c_init(struct flexcop_device *fc);
void flexcop_i2c_exit(struct flexcop_device *fc);

/* from flexcop-sram.c */
int flexcop_sram_init(struct flexcop_device *fc);

/* from flexcop-misc.c */
void flexcop_determine_revision(struct flexcop_device *fc);
void flexcop_device_name(struct flexcop_device *fc,
		const char *prefix, const char *suffix);
void flexcop_dump_reg(struct flexcop_device *fc,
		flexcop_ibi_register reg, int num);

/* from flexcop-hw-filter.c */
int flexcop_pid_feed_control(struct flexcop_device *fc,
		struct dvb_demux_feed *dvbdmxfeed, int onoff);
void flexcop_hw_filter_init(struct flexcop_device *fc);

void flexcop_smc_ctrl(struct flexcop_device *fc, int onoff);

void flexcop_set_mac_filter(struct flexcop_device *fc, u8 mac[6]);
void flexcop_mac_filter_ctrl(struct flexcop_device *fc, int onoff);

#endif
