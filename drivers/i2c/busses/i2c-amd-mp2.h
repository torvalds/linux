/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * AMD MP2 I2C adapter driver
 *
 * Authors: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 *          Elie Morisse <syniurge@gmail.com>
 */

#ifndef I2C_AMD_PCI_MP2_H
#define I2C_AMD_PCI_MP2_H

#include <linux/i2c.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>

#define PCI_DEVICE_ID_AMD_MP2	0x15E6

struct amd_i2c_common;
struct amd_mp2_dev;

enum {
	/* MP2 C2P Message Registers */
	AMD_C2P_MSG0 = 0x10500,			/* MP2 Message for I2C0 */
	AMD_C2P_MSG1 = 0x10504,			/* MP2 Message for I2C1 */
	AMD_C2P_MSG2 = 0x10508,			/* DRAM Address Lo / Data 0 */
	AMD_C2P_MSG3 = 0x1050c,			/* DRAM Address HI / Data 1 */
	AMD_C2P_MSG4 = 0x10510,			/* Data 2 */
	AMD_C2P_MSG5 = 0x10514,			/* Data 3 */
	AMD_C2P_MSG6 = 0x10518,			/* Data 4 */
	AMD_C2P_MSG7 = 0x1051c,			/* Data 5 */
	AMD_C2P_MSG8 = 0x10520,			/* Data 6 */
	AMD_C2P_MSG9 = 0x10524,			/* Data 7 */

	/* MP2 P2C Message Registers */
	AMD_P2C_MSG0 = 0x10680,			/* Do not use */
	AMD_P2C_MSG1 = 0x10684,			/* I2C0 interrupt register */
	AMD_P2C_MSG2 = 0x10688,			/* I2C1 interrupt register */
	AMD_P2C_MSG3 = 0x1068C,			/* MP2 debug info */
	AMD_P2C_MSG_INTEN = 0x10690,		/* MP2 interrupt gen register */
	AMD_P2C_MSG_INTSTS = 0x10694,		/* Interrupt status */
};

/* Command register data structures */

#define i2c_none (-1)
enum i2c_cmd {
	i2c_read = 0,
	i2c_write,
	i2c_enable,
	i2c_disable,
	number_of_sensor_discovered,
	is_mp2_active,
	invalid_cmd = 0xF,
};

enum speed_enum {
	speed100k = 0,
	speed400k = 1,
	speed1000k = 2,
	speed1400k = 3,
	speed3400k = 4
};

enum mem_type {
	use_dram = 0,
	use_c2pmsg = 1,
};

/**
 * union i2c_cmd_base : bit access of C2P commands
 * @i2c_cmd: bit 0..3 i2c R/W command
 * @bus_id: bit 4..7 i2c bus index
 * @slave_addr: bit 8..15 slave address
 * @length: bit 16..27 read/write length
 * @i2c_speed: bit 28..30 bus speed
 * @mem_type: bit 31 0-DRAM; 1-C2P msg o/p
 */
union i2c_cmd_base {
	u32 ul;
	struct {
		enum i2c_cmd i2c_cmd : 4;
		u8 bus_id : 4;
		u32 slave_addr : 8;
		u32 length : 12;
		enum speed_enum i2c_speed : 3;
		enum mem_type mem_type : 1;
	} s;
};

enum response_type {
	invalid_response = 0,
	command_success = 1,
	command_failed = 2,
};

enum status_type {
	i2c_readcomplete_event = 0,
	i2c_readfail_event = 1,
	i2c_writecomplete_event = 2,
	i2c_writefail_event = 3,
	i2c_busenable_complete = 4,
	i2c_busenable_failed = 5,
	i2c_busdisable_complete = 6,
	i2c_busdisable_failed = 7,
	invalid_data_length = 8,
	invalid_slave_address = 9,
	invalid_i2cbus_id = 10,
	invalid_dram_addr = 11,
	invalid_command = 12,
	mp2_active = 13,
	numberof_sensors_discovered_resp = 14,
	i2c_bus_notinitialized
};

/**
 * union i2c_event : bit access of P2C events
 * @response: bit 0..1 i2c response type
 * @status: bit 2..6 status_type
 * @mem_type: bit 7 0-DRAM; 1-C2P msg o/p
 * @bus_id: bit 8..11 i2c bus id
 * @length: bit 12..23 message length
 * @slave_addr: bit 24-31 slave address
 */
union i2c_event {
	u32 ul;
	struct {
		enum response_type response : 2;
		enum status_type status : 5;
		enum mem_type mem_type : 1;
		u8 bus_id : 4;
		u32 length : 12;
		u32 slave_addr : 8;
	} r;
};

/**
 * struct amd_i2c_common - per bus/i2c adapter context, shared
 *	between the pci and the platform driver
 * @eventval: MP2 event value set by the IRQ handler
 * @mp2_dev: MP2 pci device this adapter is part of
 * @msg: i2c message
 * @cmd_completion: function called by the IRQ handler to signal
 *		    the platform driver
 * @reqcmd: requested i2c command type
 * @cmd_success: set to true if the MP2 responded to a command with
 *		 the expected status and response type
 * @bus_id: bus index
 * @i2c_speed: i2c bus speed determined by the slowest slave
 * @dma_buf: if msg length > 32, holds the DMA buffer virtual address
 * @dma_addr: if msg length > 32, holds the DMA buffer address
 */
struct amd_i2c_common {
	union i2c_event eventval;
	struct amd_mp2_dev *mp2_dev;
	struct i2c_msg *msg;
	void (*cmd_completion)(struct amd_i2c_common *i2c_common);
	enum i2c_cmd reqcmd;
	u8 cmd_success;
	u8 bus_id;
	enum speed_enum i2c_speed;
	u8 *dma_buf;
	dma_addr_t dma_addr;
#ifdef CONFIG_PM
	int (*suspend)(struct amd_i2c_common *i2c_common);
	int (*resume)(struct amd_i2c_common *i2c_common);
#endif /* CONFIG_PM */
};

/**
 * struct amd_mp2_dev - per PCI device context
 * @pci_dev: PCI driver node
 * @busses: MP2 devices may have up to two busses,
 *	    each bus corresponding to an i2c adapter
 * @mmio: iommapped registers
 * @c2p_lock: controls access to the C2P mailbox shared between
 *	      the two adapters
 * @c2p_lock_busid: id of the adapter which locked c2p_lock
 */
struct amd_mp2_dev {
	struct pci_dev *pci_dev;
	struct amd_i2c_common *busses[2];
	void __iomem *mmio;
	struct mutex c2p_lock;
	u8 c2p_lock_busid;
	unsigned int probed;
	int dev_irq;
};

/* PCIe communication driver */

int amd_mp2_rw(struct amd_i2c_common *i2c_common, enum i2c_cmd reqcmd);
int amd_mp2_bus_enable_set(struct amd_i2c_common *i2c_common, bool enable);

void amd_mp2_process_event(struct amd_i2c_common *i2c_common);

void amd_mp2_rw_timeout(struct amd_i2c_common *i2c_common);

int amd_mp2_register_cb(struct amd_i2c_common *i2c_common);
int amd_mp2_unregister_cb(struct amd_i2c_common *i2c_common);

struct amd_mp2_dev *amd_mp2_find_device(void);

static inline void amd_mp2_pm_runtime_get(struct amd_mp2_dev *mp2_dev)
{
	pm_runtime_get_sync(&mp2_dev->pci_dev->dev);
}

static inline void amd_mp2_pm_runtime_put(struct amd_mp2_dev *mp2_dev)
{
	pm_runtime_put_autosuspend(&mp2_dev->pci_dev->dev);
}

#endif
