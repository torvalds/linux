/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
/**
 * @file octeon_console.c
 */
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/crc32.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "liquidio_image.h"
#include "octeon_mem_ops.h"

static void octeon_remote_lock(void);
static void octeon_remote_unlock(void);
static u64 cvmx_bootmem_phy_named_block_find(struct octeon_device *oct,
					     const char *name,
					     u32 flags);
static int octeon_console_read(struct octeon_device *oct, u32 console_num,
			       char *buffer, u32 buf_size);
static u32 console_bitmask;
module_param(console_bitmask, int, 0644);
MODULE_PARM_DESC(console_bitmask,
		 "Bitmask indicating which consoles have debug output redirected to syslog.");

#define BOOTLOADER_PCI_READ_BUFFER_DATA_ADDR    0x0006c008
#define BOOTLOADER_PCI_READ_BUFFER_LEN_ADDR     0x0006c004
#define BOOTLOADER_PCI_READ_BUFFER_OWNER_ADDR   0x0006c000
#define BOOTLOADER_PCI_READ_DESC_ADDR           0x0006c100
#define BOOTLOADER_PCI_WRITE_BUFFER_STR_LEN     248

#define OCTEON_PCI_IO_BUF_OWNER_OCTEON    0x00000001
#define OCTEON_PCI_IO_BUF_OWNER_HOST      0x00000002

/** Can change without breaking ABI */
#define CVMX_BOOTMEM_NUM_NAMED_BLOCKS 64

/** minimum alignment of bootmem alloced blocks */
#define CVMX_BOOTMEM_ALIGNMENT_SIZE     (16ull)

/** CVMX bootmem descriptor major version */
#define CVMX_BOOTMEM_DESC_MAJ_VER   3
/* CVMX bootmem descriptor minor version */
#define CVMX_BOOTMEM_DESC_MIN_VER   0

/* Current versions */
#define OCTEON_PCI_CONSOLE_MAJOR_VERSION    1
#define OCTEON_PCI_CONSOLE_MINOR_VERSION    0
#define OCTEON_PCI_CONSOLE_BLOCK_NAME   "__pci_console"
#define OCTEON_CONSOLE_POLL_INTERVAL_MS  100    /* 10 times per second */

/* First three members of cvmx_bootmem_desc are left in original
 * positions for backwards compatibility.
 * Assumes big endian target
 */
struct cvmx_bootmem_desc {
	/** spinlock to control access to list */
	u32 lock;

	/** flags for indicating various conditions */
	u32 flags;

	u64 head_addr;

	/** incremented changed when incompatible changes made */
	u32 major_version;

	/** incremented changed when compatible changes made,
	 *  reset to zero when major incremented
	 */
	u32 minor_version;

	u64 app_data_addr;
	u64 app_data_size;

	/** number of elements in named blocks array */
	u32 nb_num_blocks;

	/** length of name array in bootmem blocks */
	u32 named_block_name_len;

	/** address of named memory block descriptors */
	u64 named_block_array_addr;
};

/* Structure that defines a single console.
 *
 * Note: when read_index == write_index, the buffer is empty.
 * The actual usable size of each console is console_buf_size -1;
 */
struct octeon_pci_console {
	u64 input_base_addr;
	u32 input_read_index;
	u32 input_write_index;
	u64 output_base_addr;
	u32 output_read_index;
	u32 output_write_index;
	u32 lock;
	u32 buf_size;
};

/* This is the main container structure that contains all the information
 * about all PCI consoles.  The address of this structure is passed to various
 * routines that operation on PCI consoles.
 */
struct octeon_pci_console_desc {
	u32 major_version;
	u32 minor_version;
	u32 lock;
	u32 flags;
	u32 num_consoles;
	u32 pad;
	/* must be 64 bit aligned here... */
	/* Array of addresses of octeon_pci_console structures */
	u64 console_addr_array[0];
	/* Implicit storage for console_addr_array */
};

/**
 * \brief determines if a given console has debug enabled.
 * @param console console to check
 * @returns  1 = enabled. 0 otherwise
 */
static int octeon_console_debug_enabled(u32 console)
{
	return (console_bitmask >> (console)) & 0x1;
}

/**
 * This function is the implementation of the get macros defined
 * for individual structure members. The argument are generated
 * by the macros inorder to read only the needed memory.
 *
 * @param oct    Pointer to current octeon device
 * @param base   64bit physical address of the complete structure
 * @param offset Offset from the beginning of the structure to the member being
 *               accessed.
 * @param size   Size of the structure member.
 *
 * @return Value of the structure member promoted into a u64.
 */
static inline u64 __cvmx_bootmem_desc_get(struct octeon_device *oct,
					  u64 base,
					  u32 offset,
					  u32 size)
{
	base = (1ull << 63) | (base + offset);
	switch (size) {
	case 4:
		return octeon_read_device_mem32(oct, base);
	case 8:
		return octeon_read_device_mem64(oct, base);
	default:
		return 0;
	}
}

/**
 * This function retrieves the string name of a named block. It is
 * more complicated than a simple memcpy() since the named block
 * descriptor may not be directly accessible.
 *
 * @param addr   Physical address of the named block descriptor
 * @param str    String to receive the named block string name
 * @param len    Length of the string buffer, which must match the length
 *               stored in the bootmem descriptor.
 */
static void CVMX_BOOTMEM_NAMED_GET_NAME(struct octeon_device *oct,
					u64 addr,
					char *str,
					u32 len)
{
	addr += offsetof(struct cvmx_bootmem_named_block_desc, name);
	octeon_pci_read_core_mem(oct, addr, (u8 *)str, len);
	str[len] = 0;
}

/* See header file for descriptions of functions */

/**
 * Check the version information on the bootmem descriptor
 *
 * @param exact_match
 *               Exact major version to check against. A zero means
 *               check that the version supports named blocks.
 *
 * @return Zero if the version is correct. Negative if the version is
 *         incorrect. Failures also cause a message to be displayed.
 */
static int __cvmx_bootmem_check_version(struct octeon_device *oct,
					u32 exact_match)
{
	u32 major_version;
	u32 minor_version;

	if (!oct->bootmem_desc_addr)
		oct->bootmem_desc_addr =
			octeon_read_device_mem64(oct,
						 BOOTLOADER_PCI_READ_DESC_ADDR);
	major_version = (u32)__cvmx_bootmem_desc_get(
			oct, oct->bootmem_desc_addr,
			offsetof(struct cvmx_bootmem_desc, major_version),
			FIELD_SIZEOF(struct cvmx_bootmem_desc, major_version));
	minor_version = (u32)__cvmx_bootmem_desc_get(
			oct, oct->bootmem_desc_addr,
			offsetof(struct cvmx_bootmem_desc, minor_version),
			FIELD_SIZEOF(struct cvmx_bootmem_desc, minor_version));

	dev_dbg(&oct->pci_dev->dev, "%s: major_version=%d\n", __func__,
		major_version);
	if ((major_version > 3) ||
	    (exact_match && major_version != exact_match)) {
		dev_err(&oct->pci_dev->dev, "bootmem ver mismatch %d.%d addr:0x%llx\n",
			major_version, minor_version,
			(long long)oct->bootmem_desc_addr);
		return -1;
	} else {
		return 0;
	}
}

static const struct cvmx_bootmem_named_block_desc
*__cvmx_bootmem_find_named_block_flags(struct octeon_device *oct,
					const char *name, u32 flags)
{
	struct cvmx_bootmem_named_block_desc *desc =
		&oct->bootmem_named_block_desc;
	u64 named_addr = cvmx_bootmem_phy_named_block_find(oct, name, flags);

	if (named_addr) {
		desc->base_addr = __cvmx_bootmem_desc_get(
				oct, named_addr,
				offsetof(struct cvmx_bootmem_named_block_desc,
					 base_addr),
				FIELD_SIZEOF(
					struct cvmx_bootmem_named_block_desc,
					base_addr));
		desc->size = __cvmx_bootmem_desc_get(oct, named_addr,
				offsetof(struct cvmx_bootmem_named_block_desc,
					 size),
				FIELD_SIZEOF(
					struct cvmx_bootmem_named_block_desc,
					size));

		strncpy(desc->name, name, sizeof(desc->name));
		desc->name[sizeof(desc->name) - 1] = 0;
		return &oct->bootmem_named_block_desc;
	} else {
		return NULL;
	}
}

static u64 cvmx_bootmem_phy_named_block_find(struct octeon_device *oct,
					     const char *name,
					     u32 flags)
{
	u64 result = 0;

	if (!__cvmx_bootmem_check_version(oct, 3)) {
		u32 i;

		u64 named_block_array_addr = __cvmx_bootmem_desc_get(
					oct, oct->bootmem_desc_addr,
					offsetof(struct cvmx_bootmem_desc,
						 named_block_array_addr),
					FIELD_SIZEOF(struct cvmx_bootmem_desc,
						     named_block_array_addr));
		u32 num_blocks = (u32)__cvmx_bootmem_desc_get(
					oct, oct->bootmem_desc_addr,
					offsetof(struct cvmx_bootmem_desc,
						 nb_num_blocks),
					FIELD_SIZEOF(struct cvmx_bootmem_desc,
						     nb_num_blocks));

		u32 name_length = (u32)__cvmx_bootmem_desc_get(
					oct, oct->bootmem_desc_addr,
					offsetof(struct cvmx_bootmem_desc,
						 named_block_name_len),
					FIELD_SIZEOF(struct cvmx_bootmem_desc,
						     named_block_name_len));

		u64 named_addr = named_block_array_addr;

		for (i = 0; i < num_blocks; i++) {
			u64 named_size = __cvmx_bootmem_desc_get(
					oct, named_addr,
					 offsetof(
					struct cvmx_bootmem_named_block_desc,
					size),
					 FIELD_SIZEOF(
					struct cvmx_bootmem_named_block_desc,
					size));

			if (name && named_size) {
				char *name_tmp =
					kmalloc(name_length + 1, GFP_KERNEL);
				if (!name_tmp)
					break;

				CVMX_BOOTMEM_NAMED_GET_NAME(oct, named_addr,
							    name_tmp,
							    name_length);
				if (!strncmp(name, name_tmp, name_length)) {
					result = named_addr;
					kfree(name_tmp);
					break;
				}
				kfree(name_tmp);
			} else if (!name && !named_size) {
				result = named_addr;
				break;
			}

			named_addr +=
				sizeof(struct cvmx_bootmem_named_block_desc);
		}
	}
	return result;
}

/**
 * Find a named block on the remote Octeon
 *
 * @param name      Name of block to find
 * @param base_addr Address the block is at (OUTPUT)
 * @param size      The size of the block (OUTPUT)
 *
 * @return Zero on success, One on failure.
 */
static int octeon_named_block_find(struct octeon_device *oct, const char *name,
				   u64 *base_addr, u64 *size)
{
	const struct cvmx_bootmem_named_block_desc *named_block;

	octeon_remote_lock();
	named_block = __cvmx_bootmem_find_named_block_flags(oct, name, 0);
	octeon_remote_unlock();
	if (named_block) {
		*base_addr = named_block->base_addr;
		*size = named_block->size;
		return 0;
	}
	return 1;
}

static void octeon_remote_lock(void)
{
	/* fill this in if any sharing is needed */
}

static void octeon_remote_unlock(void)
{
	/* fill this in if any sharing is needed */
}

int octeon_console_send_cmd(struct octeon_device *oct, char *cmd_str,
			    u32 wait_hundredths)
{
	u32 len = (u32)strlen(cmd_str);

	dev_dbg(&oct->pci_dev->dev, "sending \"%s\" to bootloader\n", cmd_str);

	if (len > BOOTLOADER_PCI_WRITE_BUFFER_STR_LEN - 1) {
		dev_err(&oct->pci_dev->dev, "Command string too long, max length is: %d\n",
			BOOTLOADER_PCI_WRITE_BUFFER_STR_LEN - 1);
		return -1;
	}

	if (octeon_wait_for_bootloader(oct, wait_hundredths) != 0) {
		dev_err(&oct->pci_dev->dev, "Bootloader not ready for command.\n");
		return -1;
	}

	/* Write command to bootloader */
	octeon_remote_lock();
	octeon_pci_write_core_mem(oct, BOOTLOADER_PCI_READ_BUFFER_DATA_ADDR,
				  (u8 *)cmd_str, len);
	octeon_write_device_mem32(oct, BOOTLOADER_PCI_READ_BUFFER_LEN_ADDR,
				  len);
	octeon_write_device_mem32(oct, BOOTLOADER_PCI_READ_BUFFER_OWNER_ADDR,
				  OCTEON_PCI_IO_BUF_OWNER_OCTEON);

	/* Bootloader should accept command very quickly
	 * if it really was ready
	 */
	if (octeon_wait_for_bootloader(oct, 200) != 0) {
		octeon_remote_unlock();
		dev_err(&oct->pci_dev->dev, "Bootloader did not accept command.\n");
		return -1;
	}
	octeon_remote_unlock();
	return 0;
}

int octeon_wait_for_bootloader(struct octeon_device *oct,
			       u32 wait_time_hundredths)
{
	dev_dbg(&oct->pci_dev->dev, "waiting %d0 ms for bootloader\n",
		wait_time_hundredths);

	if (octeon_mem_access_ok(oct))
		return -1;

	while (wait_time_hundredths > 0 &&
	       octeon_read_device_mem32(oct,
					BOOTLOADER_PCI_READ_BUFFER_OWNER_ADDR)
	       != OCTEON_PCI_IO_BUF_OWNER_HOST) {
		if (--wait_time_hundredths <= 0)
			return -1;
		schedule_timeout_uninterruptible(HZ / 100);
	}
	return 0;
}

static void octeon_console_handle_result(struct octeon_device *oct,
					 size_t console_num)
{
	struct octeon_console *console;

	console = &oct->console[console_num];

	console->waiting = 0;
}

static char console_buffer[OCTEON_CONSOLE_MAX_READ_BYTES];

static void output_console_line(struct octeon_device *oct,
				struct octeon_console *console,
				size_t console_num,
				char *console_buffer,
				s32 bytes_read)
{
	char *line;
	s32 i;

	line = console_buffer;
	for (i = 0; i < bytes_read; i++) {
		/* Output a line at a time, prefixed */
		if (console_buffer[i] == '\n') {
			console_buffer[i] = '\0';
			if (console->leftover[0]) {
				dev_info(&oct->pci_dev->dev, "%lu: %s%s\n",
					 console_num, console->leftover,
					 line);
				console->leftover[0] = '\0';
			} else {
				dev_info(&oct->pci_dev->dev, "%lu: %s\n",
					 console_num, line);
			}
			line = &console_buffer[i + 1];
		}
	}

	/* Save off any leftovers */
	if (line != &console_buffer[bytes_read]) {
		console_buffer[bytes_read] = '\0';
		strcpy(console->leftover, line);
	}
}

static void check_console(struct work_struct *work)
{
	s32 bytes_read, tries, total_read;
	struct octeon_console *console;
	struct cavium_wk *wk = (struct cavium_wk *)work;
	struct octeon_device *oct = (struct octeon_device *)wk->ctxptr;
	u32 console_num = (u32)wk->ctxul;
	u32 delay;

	console = &oct->console[console_num];
	tries = 0;
	total_read = 0;

	do {
		/* Take console output regardless of whether it will
		 * be logged
		 */
		bytes_read =
			octeon_console_read(oct, console_num, console_buffer,
					    sizeof(console_buffer) - 1);
		if (bytes_read > 0) {
			total_read += bytes_read;
			if (console->waiting)
				octeon_console_handle_result(oct, console_num);
			if (octeon_console_debug_enabled(console_num)) {
				output_console_line(oct, console, console_num,
						    console_buffer, bytes_read);
			}
		} else if (bytes_read < 0) {
			dev_err(&oct->pci_dev->dev, "Error reading console %u, ret=%d\n",
				console_num, bytes_read);
		}

		tries++;
	} while ((bytes_read > 0) && (tries < 16));

	/* If nothing is read after polling the console,
	 * output any leftovers if any
	 */
	if (octeon_console_debug_enabled(console_num) &&
	    (total_read == 0) && (console->leftover[0])) {
		dev_info(&oct->pci_dev->dev, "%u: %s\n",
			 console_num, console->leftover);
		console->leftover[0] = '\0';
	}

	delay = OCTEON_CONSOLE_POLL_INTERVAL_MS;

	schedule_delayed_work(&wk->work, msecs_to_jiffies(delay));
}

int octeon_init_consoles(struct octeon_device *oct)
{
	int ret = 0;
	u64 addr, size;

	ret = octeon_mem_access_ok(oct);
	if (ret) {
		dev_err(&oct->pci_dev->dev, "Memory access not okay'\n");
		return ret;
	}

	ret = octeon_named_block_find(oct, OCTEON_PCI_CONSOLE_BLOCK_NAME, &addr,
				      &size);
	if (ret) {
		dev_err(&oct->pci_dev->dev, "Could not find console '%s'\n",
			OCTEON_PCI_CONSOLE_BLOCK_NAME);
		return ret;
	}

	/* Dedicate one of Octeon's BAR1 index registers to create a static
	 * mapping to a region of Octeon DRAM that contains the PCI console
	 * named block.
	 */
	oct->console_nb_info.bar1_index = BAR1_INDEX_STATIC_MAP;
	oct->fn_list.bar1_idx_setup(oct, addr, oct->console_nb_info.bar1_index,
				    true);
	oct->console_nb_info.dram_region_base = addr
		& ~(OCTEON_BAR1_ENTRY_SIZE - 1ULL);

	/* num_consoles > 0, is an indication that the consoles
	 * are accessible
	 */
	oct->num_consoles = octeon_read_device_mem32(oct,
		addr + offsetof(struct octeon_pci_console_desc,
			num_consoles));
	oct->console_desc_addr = addr;

	dev_dbg(&oct->pci_dev->dev, "Initialized consoles. %d available\n",
		oct->num_consoles);

	return ret;
}

int octeon_add_console(struct octeon_device *oct, u32 console_num)
{
	int ret = 0;
	u32 delay;
	u64 coreaddr;
	struct delayed_work *work;
	struct octeon_console *console;

	if (console_num >= oct->num_consoles) {
		dev_err(&oct->pci_dev->dev,
			"trying to read from console number %d when only 0 to %d exist\n",
			console_num, oct->num_consoles);
	} else {
		console = &oct->console[console_num];

		console->waiting = 0;

		coreaddr = oct->console_desc_addr + console_num * 8 +
			offsetof(struct octeon_pci_console_desc,
				 console_addr_array);
		console->addr = octeon_read_device_mem64(oct, coreaddr);
		coreaddr = console->addr + offsetof(struct octeon_pci_console,
						    buf_size);
		console->buffer_size = octeon_read_device_mem32(oct, coreaddr);
		coreaddr = console->addr + offsetof(struct octeon_pci_console,
						    input_base_addr);
		console->input_base_addr =
			octeon_read_device_mem64(oct, coreaddr);
		coreaddr = console->addr + offsetof(struct octeon_pci_console,
						    output_base_addr);
		console->output_base_addr =
			octeon_read_device_mem64(oct, coreaddr);
		console->leftover[0] = '\0';

		work = &oct->console_poll_work[console_num].work;

		INIT_DELAYED_WORK(work, check_console);
		oct->console_poll_work[console_num].ctxptr = (void *)oct;
		oct->console_poll_work[console_num].ctxul = console_num;
		delay = OCTEON_CONSOLE_POLL_INTERVAL_MS;
		schedule_delayed_work(work, msecs_to_jiffies(delay));

		if (octeon_console_debug_enabled(console_num)) {
			ret = octeon_console_send_cmd(oct,
						      "setenv pci_console_active 1",
						      2000);
		}

		console->active = 1;
	}

	return ret;
}

/**
 * Removes all consoles
 *
 * @param oct         octeon device
 */
void octeon_remove_consoles(struct octeon_device *oct)
{
	u32 i;
	struct octeon_console *console;

	for (i = 0; i < oct->num_consoles; i++) {
		console = &oct->console[i];

		if (!console->active)
			continue;

		cancel_delayed_work_sync(&oct->console_poll_work[i].
						work);
		console->addr = 0;
		console->buffer_size = 0;
		console->input_base_addr = 0;
		console->output_base_addr = 0;
	}

	oct->num_consoles = 0;
}

static inline int octeon_console_free_bytes(u32 buffer_size,
					    u32 wr_idx,
					    u32 rd_idx)
{
	if (rd_idx >= buffer_size || wr_idx >= buffer_size)
		return -1;

	return ((buffer_size - 1) - (wr_idx - rd_idx)) % buffer_size;
}

static inline int octeon_console_avail_bytes(u32 buffer_size,
					     u32 wr_idx,
					     u32 rd_idx)
{
	if (rd_idx >= buffer_size || wr_idx >= buffer_size)
		return -1;

	return buffer_size - 1 -
	       octeon_console_free_bytes(buffer_size, wr_idx, rd_idx);
}

static int octeon_console_read(struct octeon_device *oct, u32 console_num,
			       char *buffer, u32 buf_size)
{
	int bytes_to_read;
	u32 rd_idx, wr_idx;
	struct octeon_console *console;

	if (console_num >= oct->num_consoles) {
		dev_err(&oct->pci_dev->dev, "Attempted to read from disabled console %d\n",
			console_num);
		return 0;
	}

	console = &oct->console[console_num];

	/* Check to see if any data is available.
	 * Maybe optimize this with 64-bit read.
	 */
	rd_idx = octeon_read_device_mem32(oct, console->addr +
		offsetof(struct octeon_pci_console, output_read_index));
	wr_idx = octeon_read_device_mem32(oct, console->addr +
		offsetof(struct octeon_pci_console, output_write_index));

	bytes_to_read = octeon_console_avail_bytes(console->buffer_size,
						   wr_idx, rd_idx);
	if (bytes_to_read <= 0)
		return bytes_to_read;

	bytes_to_read = min_t(s32, bytes_to_read, buf_size);

	/* Check to see if what we want to read is not contiguous, and limit
	 * ourselves to the contiguous block
	 */
	if (rd_idx + bytes_to_read >= console->buffer_size)
		bytes_to_read = console->buffer_size - rd_idx;

	octeon_pci_read_core_mem(oct, console->output_base_addr + rd_idx,
				 (u8 *)buffer, bytes_to_read);
	octeon_write_device_mem32(oct, console->addr +
				  offsetof(struct octeon_pci_console,
					   output_read_index),
				  (rd_idx + bytes_to_read) %
				  console->buffer_size);

	return bytes_to_read;
}

#define FBUF_SIZE	(4 * 1024 * 1024)

int octeon_download_firmware(struct octeon_device *oct, const u8 *data,
			     size_t size)
{
	int ret = 0;
	u32 crc32_result;
	u64 load_addr;
	u32 image_len;
	struct octeon_firmware_file_header *h;
	u32 i, rem;

	if (size < sizeof(struct octeon_firmware_file_header)) {
		dev_err(&oct->pci_dev->dev, "Firmware file too small (%d < %d).\n",
			(u32)size,
			(u32)sizeof(struct octeon_firmware_file_header));
		return -EINVAL;
	}

	h = (struct octeon_firmware_file_header *)data;

	if (be32_to_cpu(h->magic) != LIO_NIC_MAGIC) {
		dev_err(&oct->pci_dev->dev, "Unrecognized firmware file.\n");
		return -EINVAL;
	}

	crc32_result = crc32((unsigned int)~0, data,
			     sizeof(struct octeon_firmware_file_header) -
			     sizeof(u32)) ^ ~0U;
	if (crc32_result != be32_to_cpu(h->crc32)) {
		dev_err(&oct->pci_dev->dev, "Firmware CRC mismatch (0x%08x != 0x%08x).\n",
			crc32_result, be32_to_cpu(h->crc32));
		return -EINVAL;
	}

	if (strncmp(LIQUIDIO_PACKAGE, h->version, strlen(LIQUIDIO_PACKAGE))) {
		dev_err(&oct->pci_dev->dev, "Unmatched firmware package type. Expected %s, got %s.\n",
			LIQUIDIO_PACKAGE, h->version);
		return -EINVAL;
	}

	if (memcmp(LIQUIDIO_BASE_VERSION, h->version + strlen(LIQUIDIO_PACKAGE),
		   strlen(LIQUIDIO_BASE_VERSION))) {
		dev_err(&oct->pci_dev->dev, "Unmatched firmware version. Expected %s.x, got %s.\n",
			LIQUIDIO_BASE_VERSION,
			h->version + strlen(LIQUIDIO_PACKAGE));
		return -EINVAL;
	}

	if (be32_to_cpu(h->num_images) > LIO_MAX_IMAGES) {
		dev_err(&oct->pci_dev->dev, "Too many images in firmware file (%d).\n",
			be32_to_cpu(h->num_images));
		return -EINVAL;
	}

	dev_info(&oct->pci_dev->dev, "Firmware version: %s\n", h->version);
	snprintf(oct->fw_info.liquidio_firmware_version, 32, "LIQUIDIO: %s",
		 h->version);

	data += sizeof(struct octeon_firmware_file_header);

	dev_info(&oct->pci_dev->dev, "%s: Loading %d images\n", __func__,
		 be32_to_cpu(h->num_images));
	/* load all images */
	for (i = 0; i < be32_to_cpu(h->num_images); i++) {
		load_addr = be64_to_cpu(h->desc[i].addr);
		image_len = be32_to_cpu(h->desc[i].len);

		dev_info(&oct->pci_dev->dev, "Loading firmware %d at %llx\n",
			 image_len, load_addr);

		/* Write in 4MB chunks*/
		rem = image_len;

		while (rem) {
			if (rem < FBUF_SIZE)
				size = rem;
			else
				size = FBUF_SIZE;

			/* download the image */
			octeon_pci_write_core_mem(oct, load_addr, data, (u32)size);

			data += size;
			rem -= (u32)size;
			load_addr += size;
		}
	}
	dev_info(&oct->pci_dev->dev, "Writing boot command: %s\n",
		 h->bootcmd);

	/* Invoke the bootcmd */
	ret = octeon_console_send_cmd(oct, h->bootcmd, 50);

	return 0;
}
