/* SPDX-License-Identifier: GPL-2.0
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright (C) 2018 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   BSD LICENSE
 *
 *   Copyright (C) 2018 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copy
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of AMD Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * AMD PCIe MP2 Communication Interface Driver
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#ifndef I2C_AMD_PCI_MP2_H
#define I2C_AMD_PCI_MP2_H

#include <linux/pci.h>

#define PCI_DEVICE_ID_AMD_MP2	0x15E6

#define write64 _write64
static inline void _write64(u64 val, void __iomem *mmio)
{
	writel(val, mmio);
	writel(val >> 32, mmio + sizeof(u32));
}

#define read64 _read64
static inline u64 _read64(void __iomem *mmio)
{
	u64 low, high;

	low = readl(mmio);
	high = readl(mmio + sizeof(u32));
	return low | (high << 32);
}

enum {
	/* MP2 C2P Message Registers */
	AMD_C2P_MSG0 = 0x10500, /*MP2 Message for I2C0*/
	AMD_C2P_MSG1 = 0x10504, /*MP2 Message for I2C1*/
	AMD_C2P_MSG2 = 0x10508, /*DRAM Address Lo / Data 0*/
	AMD_C2P_MSG3 = 0x1050c, /*DRAM Address HI / Data 1*/
	AMD_C2P_MSG4 = 0x10510, /*Data 2*/
	AMD_C2P_MSG5 = 0x10514, /*Data 3*/
	AMD_C2P_MSG6 = 0x10518, /*Data 4*/
	AMD_C2P_MSG7 = 0x1051c, /*Data 5*/
	AMD_C2P_MSG8 = 0x10520, /*Data 6*/
	AMD_C2P_MSG9 = 0x10524, /*Data 7*/

	/* MP2 P2C Message Registers */
	AMD_P2C_MSG0 = 0x10680, /*Do not use*/
	AMD_P2C_MSG1 = 0x10684, /*I2c0 int reg*/
	AMD_P2C_MSG2 = 0x10688, /*I2c1 int reg*/
	AMD_P2C_MSG3 = 0x1068C, /*MP2 debug info*/
	AMD_P2C_MSG_INTEN = 0x10690, /*MP2 int gen register*/
	AMD_P2C_MSG_INTSTS = 0x10694, /*Interrupt sts*/
};

/* Command register data structures */

enum i2c_cmd {
	i2c_read,
	i2c_write,
	i2c_enable,
	i2c_disable,
	number_of_sensor_discovered,
	is_mp2_active,
	invalid_cmd = 0xF,
};

enum i2c_bus_index {
	i2c_bus_0 = 0,
	i2c_bus_1 = 1,
	i2c_bus_max
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

union i2c_cmd_base {
	u32 ul;
	struct {
		enum i2c_cmd i2c_cmd : 4; /*!< bit: 0..3 i2c R/W command */
		enum i2c_bus_index bus_id : 4; /*!< bit: 4..7 i2c bus index */
		u32 dev_addr : 8; /*!< bit: 8..15 device address or Bus Speed*/
		u32 length : 12; /*!< bit: 16..29 read/write length */
		enum speed_enum i2c_speed : 3; /*!< bit: 30 register address*/
		enum mem_type mem_type : 1; /*!< bit: 15 mem type*/
	} s; /*!< Structure used for bit access */
};

/* Response register data structures */

/*Response - Response of SFI*/
enum response_type {
	invalid_response = 0,
	command_success = 1,
	command_failed = 2,
};

/*Status - Command ID to indicate a command*/
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
	i2C_bus_notinitialized
};

struct i2c_event {
	union {
		u32 ul;
		struct {
			enum response_type response : 2; /*!< bit: 0..1 I2C res type */
			enum status_type status : 5; /*!< bit: 2..6 status_type */
			enum mem_type mem_type : 1; /*!< bit: 7 0-DRAM;1- C2PMsg o/p */
			enum i2c_bus_index bus_id : 4; /*!< bit: 8..11 I2C Bus ID */
			u32 length : 12; /*!< bit:16..29 length */
			u32 slave_addr : 8; /*!< bit: 15 debug msg include in p2c msg */
		} r; /*!< Structure used for bit access */
	} base;
	u32 *buf;
};

/* data structures for communication with I2c*/

struct i2c_connect_config {
	enum i2c_bus_index bus_id;
	u64 i2c_speed;
	u16 dev_addr;
};

struct i2c_write_config {
	enum i2c_bus_index bus_id;
	u64 i2c_speed;
	u16 dev_addr;
	u32 length;
	phys_addr_t phy_addr;
	u32 *buf;
};

struct i2c_read_config {
	enum i2c_bus_index bus_id;
	u64 i2c_speed;
	u16 dev_addr;
	u32 length;
	phys_addr_t phy_addr;
	u8 *buf;
};

// struct to send/receive data b/w pci and i2c drivers
struct amd_i2c_pci_ops {
	int (*read_complete)(struct i2c_event event, void *dev_ctx);
	int (*write_complete)(struct i2c_event event, void *dev_ctx);
	int (*connect_complete)(struct i2c_event event, void *dev_ctx);
};

struct amd_i2c_common {
	struct i2c_connect_config connect_cfg;
	struct i2c_read_config read_cfg;
	struct i2c_write_config write_cfg;
	const struct amd_i2c_pci_ops *ops;
	struct pci_dev *pdev;
};

struct amd_mp2_dev {
	struct pci_dev *pdev;
	struct dentry *debugfs_dir;
	struct dentry *debugfs_info;
	void __iomem *mmio;
	struct i2c_event eventval;
	enum i2c_cmd reqcmd;
	struct i2c_connect_config connect_cfg;
	struct i2c_read_config read_cfg;
	struct i2c_write_config write_cfg;
	union i2c_cmd_base i2c_cmd_base;
	const struct amd_i2c_pci_ops *ops;
	struct delayed_work work;
	void *i2c_dev_ctx;
	bool	requested;
	raw_spinlock_t lock;
};

int amd_mp2_read(struct pci_dev *pdev, struct i2c_read_config read_cfg);
int amd_mp2_write(struct pci_dev *pdev,
		  struct i2c_write_config write_cfg);
int amd_mp2_connect(struct pci_dev *pdev,
		    struct i2c_connect_config connect_cfg);
int amd_i2c_register_cb(struct pci_dev *pdev, const struct amd_i2c_pci_ops *ops,
			void *dev_ctx);

#define ndev_pdev(ndev) ((ndev)->pdev)
#define ndev_name(ndev) pci_name(ndev_pdev(ndev))
#define ndev_dev(ndev) (&ndev_pdev(ndev)->dev)
#define mp2_dev(__work) container_of(__work, struct amd_mp2_dev, work.work)

#endif
