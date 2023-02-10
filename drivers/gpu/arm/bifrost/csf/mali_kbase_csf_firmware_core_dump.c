// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2021-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/file.h>
#include <linux/elf.h>
#include <linux/elfcore.h>

#include "mali_kbase.h"
#include "mali_kbase_csf_firmware_core_dump.h"
#include "backend/gpu/mali_kbase_pm_internal.h"

/* Page size in bytes in use by MCU. */
#define FW_PAGE_SIZE 4096

/*
 * FW image header core dump data format supported.
 * Currently only version 0.1 is supported.
 */
#define FW_CORE_DUMP_DATA_VERSION_MAJOR 0
#define FW_CORE_DUMP_DATA_VERSION_MINOR 1

/* Full version of the image header core dump data format */
#define FW_CORE_DUMP_DATA_VERSION                                                                  \
	((FW_CORE_DUMP_DATA_VERSION_MAJOR << 8) | FW_CORE_DUMP_DATA_VERSION_MINOR)

/* Validity flag to indicate if the MCU registers in the buffer are valid */
#define FW_MCU_STATUS_MASK 0x1
#define FW_MCU_STATUS_VALID (1 << 0)

/* Core dump entry fields */
#define FW_CORE_DUMP_VERSION_INDEX 0
#define FW_CORE_DUMP_START_ADDR_INDEX 1

/* MCU registers stored by a firmware core dump */
struct fw_core_dump_mcu {
	u32 r0;
	u32 r1;
	u32 r2;
	u32 r3;
	u32 r4;
	u32 r5;
	u32 r6;
	u32 r7;
	u32 r8;
	u32 r9;
	u32 r10;
	u32 r11;
	u32 r12;
	u32 sp;
	u32 lr;
	u32 pc;
};

/* Any ELF definitions used in this file are from elf.h/elfcore.h except
 * when specific 32-bit versions are required (mainly for the
 * ELF_PRSTATUS32 note that is used to contain the MCU registers).
 */

/* - 32-bit version of timeval structures used in ELF32 PRSTATUS note. */
struct prstatus32_timeval {
	int tv_sec;
	int tv_usec;
};

/* - Structure defining ELF32 PRSTATUS note contents, as defined by the
 *   GNU binutils BFD library used by GDB, in bfd/hosts/x86-64linux.h.
 *   Note: GDB checks for the size of this structure to be 0x94.
 *   Modified pr_reg (array containing the Arm 32-bit MCU registers) to
 *   use u32[18] instead of elf_gregset32_t to prevent introducing new typedefs.
 */
struct elf_prstatus32 {
	struct elf_siginfo pr_info;		/* Info associated with signal. */
	short int pr_cursig;			/* Current signal. */
	unsigned int pr_sigpend;		/* Set of pending signals. */
	unsigned int pr_sighold;		/* Set of held signals. */
	pid_t pr_pid;
	pid_t pr_ppid;
	pid_t pr_pgrp;
	pid_t pr_sid;
	struct prstatus32_timeval pr_utime;	/* User time. */
	struct prstatus32_timeval pr_stime;	/* System time. */
	struct prstatus32_timeval pr_cutime;	/* Cumulative user time. */
	struct prstatus32_timeval pr_cstime;	/* Cumulative system time. */
	u32 pr_reg[18];				/* GP registers. */
	int pr_fpvalid;				/* True if math copro being used. */
};

/**
 * struct fw_core_dump_data - Context for seq_file operations used on 'fw_core_dump'
 * debugfs file.
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 */
struct fw_core_dump_data {
	struct kbase_device *kbdev;
};

/*
 * struct fw_core_dump_seq_off - Iterator for seq_file operations used on 'fw_core_dump'
 * debugfs file.
 * @interface: current firmware memory interface
 * @page_num: current page number (0..) within @interface
 */
struct fw_core_dump_seq_off {
	struct kbase_csf_firmware_interface *interface;
	u32 page_num;
};

/**
 * fw_get_core_dump_mcu - Get the MCU registers saved by a firmware core dump
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 * @regs:  Pointer to a core dump mcu struct where the MCU registers are copied
 *         to. Should be allocated by the called.
 *
 * Return: 0 if successfully copied the MCU registers, negative error code otherwise.
 */
static int fw_get_core_dump_mcu(struct kbase_device *kbdev, struct fw_core_dump_mcu *regs)
{
	unsigned int i;
	u32 status = 0;
	u32 data_addr = kbdev->csf.fw_core_dump.mcu_regs_addr;
	u32 *data = (u32 *)regs;

	/* Check if the core dump entry exposed the buffer */
	if (!regs || !kbdev->csf.fw_core_dump.available)
		return -EPERM;

	/* Check if the data in the buffer is valid, if not, return error */
	kbase_csf_read_firmware_memory(kbdev, data_addr, &status);
	if ((status & FW_MCU_STATUS_MASK) != FW_MCU_STATUS_VALID)
		return -EPERM;

	/* According to image header documentation, the MCU registers core dump
	 * buffer is 32-bit aligned.
	 */
	for (i = 1; i <= sizeof(struct fw_core_dump_mcu) / sizeof(u32); ++i)
		kbase_csf_read_firmware_memory(kbdev, data_addr + i * sizeof(u32), &data[i - 1]);

	return 0;
}

/**
 * fw_core_dump_fill_elf_header - Initializes an ELF32 header
 * @hdr:	ELF32 header to initialize
 * @sections:	Number of entries in the ELF program header table
 *
 * Initializes an ELF32 header for an ARM 32-bit little-endian
 * 'Core file' object file.
 */
static void fw_core_dump_fill_elf_header(struct elf32_hdr *hdr, unsigned int sections)
{
	/* Reset all members in header. */
	memset(hdr, 0, sizeof(*hdr));

	/* Magic number identifying file as an ELF object. */
	memcpy(hdr->e_ident, ELFMAG, SELFMAG);

	/* Identify file as 32-bit, little-endian, using current
	 * ELF header version, with no OS or ABI specific ELF
	 * extensions used.
	 */
	hdr->e_ident[EI_CLASS] = ELFCLASS32;
	hdr->e_ident[EI_DATA] = ELFDATA2LSB;
	hdr->e_ident[EI_VERSION] = EV_CURRENT;
	hdr->e_ident[EI_OSABI] = ELFOSABI_NONE;

	/* 'Core file' type of object file. */
	hdr->e_type = ET_CORE;

	/* ARM 32-bit architecture (AARCH32) */
	hdr->e_machine = EM_ARM;

	/* Object file version: the original format. */
	hdr->e_version = EV_CURRENT;

	/* Offset of program header table in file. */
	hdr->e_phoff = sizeof(struct elf32_hdr);

	/* No processor specific flags. */
	hdr->e_flags = 0;

	/* Size of the ELF header in bytes. */
	hdr->e_ehsize = sizeof(struct elf32_hdr);

	/* Size of the ELF program header entry in bytes. */
	hdr->e_phentsize = sizeof(struct elf32_phdr);

	/* Number of entries in the program header table. */
	hdr->e_phnum = sections;
}

/**
 * fw_core_dump_fill_elf_program_header_note - Initializes an ELF32 program header
 * for holding auxiliary information
 * @phdr:		ELF32 program header
 * @file_offset:	Location of the note in the file in bytes
 * @size:		Size of the note in bytes.
 *
 * Initializes an ELF32 program header describing auxiliary information (containing
 * one or more notes) of @size bytes alltogether located in the file at offset
 * @file_offset.
 */
static void fw_core_dump_fill_elf_program_header_note(struct elf32_phdr *phdr, u32 file_offset,
						      u32 size)
{
	/* Auxiliary information (note) in program header. */
	phdr->p_type = PT_NOTE;

	/* Location of first note in file in bytes. */
	phdr->p_offset = file_offset;

	/* Size of all notes combined in bytes. */
	phdr->p_filesz = size;

	/* Other members not relevant for a note. */
	phdr->p_vaddr = 0;
	phdr->p_paddr = 0;
	phdr->p_memsz = 0;
	phdr->p_align = 0;
	phdr->p_flags = 0;
}

/**
 * fw_core_dump_fill_elf_program_header - Initializes an ELF32 program header for a loadable segment
 * @phdr:		ELF32 program header to initialize.
 * @file_offset:	Location of loadable segment in file in bytes
 *                      (aligned to FW_PAGE_SIZE bytes)
 * @vaddr:		32-bit virtual address where to write the segment
 *                      (aligned to FW_PAGE_SIZE bytes)
 * @size:		Size of the segment in bytes.
 * @flags:		CSF_FIRMWARE_ENTRY_* flags describing access permissions.
 *
 * Initializes an ELF32 program header describing a loadable segment of
 * @size bytes located in the file at offset @file_offset to be loaded
 * at virtual address @vaddr with access permissions as described by
 * CSF_FIRMWARE_ENTRY_* flags in @flags.
 */
static void fw_core_dump_fill_elf_program_header(struct elf32_phdr *phdr, u32 file_offset,
						 u32 vaddr, u32 size, u32 flags)
{
	/* Loadable segment in program header. */
	phdr->p_type = PT_LOAD;

	/* Location of segment in file in bytes. Aligned to p_align bytes. */
	phdr->p_offset = file_offset;

	/* Virtual address of segment. Aligned to p_align bytes. */
	phdr->p_vaddr = vaddr;

	/* Physical address of segment. Not relevant. */
	phdr->p_paddr = 0;

	/* Size of segment in file and memory. */
	phdr->p_filesz = size;
	phdr->p_memsz = size;

	/* Alignment of segment in the file and memory in bytes (integral power of 2). */
	phdr->p_align = FW_PAGE_SIZE;

	/* Set segment access permissions. */
	phdr->p_flags = 0;
	if (flags & CSF_FIRMWARE_ENTRY_READ)
		phdr->p_flags |= PF_R;
	if (flags & CSF_FIRMWARE_ENTRY_WRITE)
		phdr->p_flags |= PF_W;
	if (flags & CSF_FIRMWARE_ENTRY_EXECUTE)
		phdr->p_flags |= PF_X;
}

/**
 * fw_core_dump_get_prstatus_note_size - Calculates size of a ELF32 PRSTATUS note
 * @name:	Name given to the PRSTATUS note.
 *
 * Calculates the size of a 32-bit PRSTATUS note (which contains information
 * about a process like the current MCU registers) taking into account
 * @name must be padded to a 4-byte multiple.
 *
 * Return: size of 32-bit PRSTATUS note in bytes.
 */
static unsigned int fw_core_dump_get_prstatus_note_size(char *name)
{
	return sizeof(struct elf32_note) + roundup(strlen(name) + 1, 4) +
	       sizeof(struct elf_prstatus32);
}

/**
 * fw_core_dump_fill_elf_prstatus - Initializes an ELF32 PRSTATUS structure
 * @prs:	ELF32 PRSTATUS note to initialize
 * @regs:	MCU registers to copy into the PRSTATUS note
 *
 * Initializes an ELF32 PRSTATUS structure with MCU registers @regs.
 * Other process information is N/A for CSF Firmware.
 */
static void fw_core_dump_fill_elf_prstatus(struct elf_prstatus32 *prs,
					   struct fw_core_dump_mcu *regs)
{
	/* Only fill in registers (32-bit) of PRSTATUS note. */
	memset(prs, 0, sizeof(*prs));
	prs->pr_reg[0] = regs->r0;
	prs->pr_reg[1] = regs->r1;
	prs->pr_reg[2] = regs->r2;
	prs->pr_reg[3] = regs->r3;
	prs->pr_reg[4] = regs->r4;
	prs->pr_reg[5] = regs->r5;
	prs->pr_reg[6] = regs->r0;
	prs->pr_reg[7] = regs->r7;
	prs->pr_reg[8] = regs->r8;
	prs->pr_reg[9] = regs->r9;
	prs->pr_reg[10] = regs->r10;
	prs->pr_reg[11] = regs->r11;
	prs->pr_reg[12] = regs->r12;
	prs->pr_reg[13] = regs->sp;
	prs->pr_reg[14] = regs->lr;
	prs->pr_reg[15] = regs->pc;
}

/**
 * fw_core_dump_create_prstatus_note - Creates an ELF32 PRSTATUS note
 * @name:	Name for the PRSTATUS note
 * @prs:	ELF32 PRSTATUS structure to put in the PRSTATUS note
 * @created_prstatus_note:
 *		Pointer to the allocated ELF32 PRSTATUS note
 *
 * Creates an ELF32 note with one PRSTATUS entry containing the
 * ELF32 PRSTATUS structure @prs. Caller needs to free the created note in
 * @created_prstatus_note.
 *
 * Return: 0 on failure, otherwise size of ELF32 PRSTATUS note in bytes.
 */
static unsigned int fw_core_dump_create_prstatus_note(char *name, struct elf_prstatus32 *prs,
						      struct elf32_note **created_prstatus_note)
{
	struct elf32_note *note;
	unsigned int note_name_sz;
	unsigned int note_sz;

	/* Allocate memory for ELF32 note containing a PRSTATUS note. */
	note_name_sz = strlen(name) + 1;
	note_sz = sizeof(struct elf32_note) + roundup(note_name_sz, 4) +
		  sizeof(struct elf_prstatus32);
	note = kmalloc(note_sz, GFP_KERNEL);
	if (!note)
		return 0;

	/* Fill in ELF32 note with one entry for a PRSTATUS note. */
	note->n_namesz = note_name_sz;
	note->n_descsz = sizeof(struct elf_prstatus32);
	note->n_type = NT_PRSTATUS;
	memcpy(note + 1, name, note_name_sz);
	memcpy((char *)(note + 1) + roundup(note_name_sz, 4), prs, sizeof(*prs));

	/* Return pointer and size of the created ELF32 note. */
	*created_prstatus_note = note;
	return note_sz;
}

/**
 * fw_core_dump_write_elf_header - Writes ELF header for the FW core dump
 * @m: the seq_file handle
 *
 * Writes the ELF header of the core dump including program headers for
 * memory sections and a note containing the current MCU register
 * values.
 *
 * Excludes memory sections without read access permissions or
 * are for protected memory.
 *
 * The data written is as follows:
 * - ELF header
 * - ELF PHDRs for memory sections
 * - ELF PHDR for program header NOTE
 * - ELF PRSTATUS note
 * - 0-bytes padding to multiple of ELF_EXEC_PAGESIZE
 *
 * The actual memory section dumps should follow this (not written
 * by this function).
 *
 * Retrieves the necessary information via the struct
 * fw_core_dump_data stored in the private member of the seq_file
 * handle.
 *
 * Return:
 * * 0		- success
 * * -ENOMEM	- not enough memory for allocating ELF32 note
 */
static int fw_core_dump_write_elf_header(struct seq_file *m)
{
	struct elf32_hdr hdr;
	struct elf32_phdr phdr;
	struct fw_core_dump_data *dump_data = m->private;
	struct kbase_device *const kbdev = dump_data->kbdev;
	struct kbase_csf_firmware_interface *interface;
	struct elf_prstatus32 elf_prs;
	struct elf32_note *elf_prstatus_note;
	unsigned int sections = 0;
	unsigned int elf_prstatus_note_size;
	u32 elf_prstatus_offset;
	u32 elf_phdr_note_offset;
	u32 elf_memory_sections_data_offset;
	u32 total_pages = 0;
	u32 padding_size, *padding;
	struct fw_core_dump_mcu regs = { 0 };

	/* Count number of memory sections. */
	list_for_each_entry(interface, &kbdev->csf.firmware_interfaces, node) {
		/* Skip memory sections that cannot be read or are protected. */
		if ((interface->flags & CSF_FIRMWARE_ENTRY_PROTECTED) ||
		    (interface->flags & CSF_FIRMWARE_ENTRY_READ) == 0)
			continue;
		sections++;
	}

	/* Prepare ELF header. */
	fw_core_dump_fill_elf_header(&hdr, sections + 1);
	seq_write(m, &hdr, sizeof(struct elf32_hdr));

	elf_prstatus_note_size = fw_core_dump_get_prstatus_note_size("CORE");
	/* PHDRs of PT_LOAD type. */
	elf_phdr_note_offset = sizeof(struct elf32_hdr) + sections * sizeof(struct elf32_phdr);
	/* PHDR of PT_NOTE type. */
	elf_prstatus_offset = elf_phdr_note_offset + sizeof(struct elf32_phdr);
	elf_memory_sections_data_offset = elf_prstatus_offset + elf_prstatus_note_size;

	/* Calculate padding size to page offset. */
	padding_size = roundup(elf_memory_sections_data_offset, ELF_EXEC_PAGESIZE) -
		       elf_memory_sections_data_offset;
	elf_memory_sections_data_offset += padding_size;

	/* Prepare ELF program header table. */
	list_for_each_entry(interface, &kbdev->csf.firmware_interfaces, node) {
		/* Skip memory sections that cannot be read or are protected. */
		if ((interface->flags & CSF_FIRMWARE_ENTRY_PROTECTED) ||
		    (interface->flags & CSF_FIRMWARE_ENTRY_READ) == 0)
			continue;

		fw_core_dump_fill_elf_program_header(&phdr, elf_memory_sections_data_offset,
						     interface->virtual,
						     interface->num_pages * FW_PAGE_SIZE,
						     interface->flags);

		seq_write(m, &phdr, sizeof(struct elf32_phdr));

		elf_memory_sections_data_offset += interface->num_pages * FW_PAGE_SIZE;
		total_pages += interface->num_pages;
	}

	/* Prepare PHDR of PT_NOTE type. */
	fw_core_dump_fill_elf_program_header_note(&phdr, elf_prstatus_offset,
						  elf_prstatus_note_size);
	seq_write(m, &phdr, sizeof(struct elf32_phdr));

	/* Prepare ELF note of PRSTATUS type. */
	if (fw_get_core_dump_mcu(kbdev, &regs))
		dev_dbg(kbdev->dev, "MCU Registers not available, all registers set to zero");
	/* Even if MCU Registers are not available the ELF prstatus is still
	 * filled with the registers equal to zero.
	 */
	fw_core_dump_fill_elf_prstatus(&elf_prs, &regs);
	elf_prstatus_note_size =
		fw_core_dump_create_prstatus_note("CORE", &elf_prs, &elf_prstatus_note);
	if (elf_prstatus_note_size == 0)
		return -ENOMEM;

	seq_write(m, elf_prstatus_note, elf_prstatus_note_size);
	kfree(elf_prstatus_note);

	/* Pad file to page size. */
	padding = kzalloc(padding_size, GFP_KERNEL);
	seq_write(m, padding, padding_size);
	kfree(padding);

	return 0;
}

/**
 * fw_core_dump_create - Requests firmware to save state for a firmware core dump
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * Return: 0 on success, error code otherwise.
 */
static int fw_core_dump_create(struct kbase_device *kbdev)
{
	int err;

	/* Ensure MCU is active before requesting the core dump. */
	kbase_csf_scheduler_pm_active(kbdev);
	err = kbase_csf_scheduler_wait_mcu_active(kbdev);
	if (!err)
		err = kbase_csf_firmware_req_core_dump(kbdev);

	kbase_csf_scheduler_pm_idle(kbdev);

	return err;
}

/**
 * fw_core_dump_seq_start - seq_file start operation for firmware core dump file
 * @m: the seq_file handle
 * @_pos: holds the current position in pages
 *        (0 or most recent position used in previous session)
 *
 * Starts a seq_file session, positioning the iterator for the session to page @_pos - 1
 * within the firmware interface memory sections. @_pos value 0 is used to indicate the
 * position of the ELF header at the start of the file.
 *
 * Retrieves the necessary information via the struct fw_core_dump_data stored in
 * the private member of the seq_file handle.
 *
 * Return:
 * * iterator pointer	- pointer to iterator struct fw_core_dump_seq_off
 * * SEQ_START_TOKEN	- special iterator pointer indicating its is the start of the file
 * * NULL		- iterator could not be allocated
 */
static void *fw_core_dump_seq_start(struct seq_file *m, loff_t *_pos)
{
	struct fw_core_dump_data *dump_data = m->private;
	struct fw_core_dump_seq_off *data;
	struct kbase_csf_firmware_interface *interface;
	loff_t pos = *_pos;

	if (pos == 0)
		return SEQ_START_TOKEN;

	/* Move iterator in the right position based on page number within
	 * available pages of firmware interface memory sections.
	 */
	pos--; /* ignore start token */
	list_for_each_entry(interface, &dump_data->kbdev->csf.firmware_interfaces, node) {
		/* Skip memory sections that cannot be read or are protected. */
		if ((interface->flags & CSF_FIRMWARE_ENTRY_PROTECTED) ||
		    (interface->flags & CSF_FIRMWARE_ENTRY_READ) == 0)
			continue;

		if (pos >= interface->num_pages) {
			pos -= interface->num_pages;
		} else {
			data = kmalloc(sizeof(*data), GFP_KERNEL);
			if (!data)
				return NULL;
			data->interface = interface;
			data->page_num = pos;
			return data;
		}
	}

	return NULL;
}

/**
 * fw_core_dump_seq_stop - seq_file stop operation for firmware core dump file
 * @m: the seq_file handle
 * @v: the current iterator (pointer to struct fw_core_dump_seq_off)
 *
 * Closes the current session and frees any memory related.
 */
static void fw_core_dump_seq_stop(struct seq_file *m, void *v)
{
	kfree(v);
}

/**
 * fw_core_dump_seq_next - seq_file next operation for firmware core dump file
 * @m: the seq_file handle
 * @v: the current iterator (pointer to struct fw_core_dump_seq_off)
 * @pos: holds the current position in pages
 *        (0 or most recent position used in previous session)
 *
 * Moves the iterator @v forward to the next page within the firmware interface
 * memory sections and returns the updated position in @pos.
 * @v value SEQ_START_TOKEN indicates the ELF header position.
 *
 * Return:
 * * iterator pointer	- pointer to iterator struct fw_core_dump_seq_off
 * * NULL		- iterator could not be allocated
 */
static void *fw_core_dump_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct fw_core_dump_data *dump_data = m->private;
	struct fw_core_dump_seq_off *data = v;
	struct kbase_csf_firmware_interface *interface;
	struct list_head *interfaces = &dump_data->kbdev->csf.firmware_interfaces;

	/* Is current position at the ELF header ? */
	if (v == SEQ_START_TOKEN) {
		if (list_empty(interfaces))
			return NULL;

		/* Prepare iterator for starting at first page in firmware interface
		 * memory sections.
		 */
		data = kmalloc(sizeof(*data), GFP_KERNEL);
		if (!data)
			return NULL;
		data->interface =
			list_first_entry(interfaces, struct kbase_csf_firmware_interface, node);
		data->page_num = 0;
		++*pos;
		return data;
	}

	/* First attempt to satisfy from current firmware interface memory section. */
	interface = data->interface;
	if (data->page_num + 1 < interface->num_pages) {
		data->page_num++;
		++*pos;
		return data;
	}

	/* Need next firmware interface memory section. This could be the last one. */
	if (list_is_last(&interface->node, interfaces)) {
		kfree(data);
		return NULL;
	}

	/* Move to first page in next firmware interface memory section. */
	data->interface = list_next_entry(interface, node);
	data->page_num = 0;
	++*pos;

	return data;
}

/**
 * fw_core_dump_seq_show - seq_file show operation for firmware core dump file
 * @m: the seq_file handle
 * @v: the current iterator (pointer to struct fw_core_dump_seq_off)
 *
 * Writes the current page in a firmware interface memory section indicated
 * by the iterator @v to the file. If @v is SEQ_START_TOKEN the ELF
 * header is written.
 *
 * Return: 0 on success, error code otherwise.
 */
static int fw_core_dump_seq_show(struct seq_file *m, void *v)
{
	struct fw_core_dump_seq_off *data = v;
	struct page *page;
	u32 *p;

	/* Either write the ELF header or current page. */
	if (v == SEQ_START_TOKEN)
		return fw_core_dump_write_elf_header(m);

	/* Write the current page. */
	page = as_page(data->interface->phys[data->page_num]);
	p = kmap_atomic(page);
	seq_write(m, p, FW_PAGE_SIZE);
	kunmap_atomic(p);

	return 0;
}

/* Sequence file operations for firmware core dump file. */
static const struct seq_operations fw_core_dump_seq_ops = {
	.start = fw_core_dump_seq_start,
	.next = fw_core_dump_seq_next,
	.stop = fw_core_dump_seq_stop,
	.show = fw_core_dump_seq_show,
};

/**
 * fw_core_dump_debugfs_open - callback for opening the 'fw_core_dump' debugfs file
 * @inode: inode of the file
 * @file:  file pointer
 *
 * Prepares for servicing a write request to request a core dump from firmware and
 * a read request to retrieve the core dump.
 *
 * Returns an error if the firmware is not initialized yet.
 *
 * Return: 0 on success, error code otherwise.
 */
static int fw_core_dump_debugfs_open(struct inode *inode, struct file *file)
{
	struct kbase_device *const kbdev = inode->i_private;
	struct fw_core_dump_data *dump_data;
	int ret;

	/* Fail if firmware is not initialized yet. */
	if (!kbdev->csf.firmware_inited) {
		ret = -ENODEV;
		goto open_fail;
	}

	/* Open a sequence file for iterating through the pages in the
	 * firmware interface memory pages. seq_open stores a
	 * struct seq_file * in the private_data field of @file.
	 */
	ret = seq_open(file, &fw_core_dump_seq_ops);
	if (ret)
		goto open_fail;

	/* Allocate a context for sequence file operations. */
	dump_data = kmalloc(sizeof(*dump_data), GFP_KERNEL);
	if (!dump_data) {
		ret = -ENOMEM;
		goto out;
	}

	/* Kbase device will be shared with sequence file operations. */
	dump_data->kbdev = kbdev;

	/* Link our sequence file context. */
	((struct seq_file *)file->private_data)->private = dump_data;

	return 0;
out:
	seq_release(inode, file);
open_fail:
	return ret;
}

/**
 * fw_core_dump_debugfs_write - callback for a write to the 'fw_core_dump' debugfs file
 * @file:  file pointer
 * @ubuf:  user buffer containing data to store
 * @count: number of bytes in user buffer
 * @ppos:  file position
 *
 * Any data written to the file triggers a firmware core dump request which
 * subsequently can be retrieved by reading from the file.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t fw_core_dump_debugfs_write(struct file *file, const char __user *ubuf, size_t count,
					  loff_t *ppos)
{
	int err;
	struct fw_core_dump_data *dump_data = ((struct seq_file *)file->private_data)->private;
	struct kbase_device *const kbdev = dump_data->kbdev;

	CSTD_UNUSED(ppos);

	err = fw_core_dump_create(kbdev);

	return err ? err : count;
}

/**
 * fw_core_dump_debugfs_release - callback for releasing the 'fw_core_dump' debugfs file
 * @inode: inode of the file
 * @file:  file pointer
 *
 * Return: 0 on success, error code otherwise.
 */
static int fw_core_dump_debugfs_release(struct inode *inode, struct file *file)
{
	struct fw_core_dump_data *dump_data = ((struct seq_file *)file->private_data)->private;

	seq_release(inode, file);

	kfree(dump_data);

	return 0;
}
/* Debugfs file operations for firmware core dump file. */
static const struct file_operations kbase_csf_fw_core_dump_fops = {
	.owner = THIS_MODULE,
	.open = fw_core_dump_debugfs_open,
	.read = seq_read,
	.write = fw_core_dump_debugfs_write,
	.llseek = seq_lseek,
	.release = fw_core_dump_debugfs_release,
};

void kbase_csf_firmware_core_dump_init(struct kbase_device *const kbdev)
{
#if IS_ENABLED(CONFIG_DEBUG_FS)
	debugfs_create_file("fw_core_dump", 0600, kbdev->mali_debugfs_directory, kbdev,
			    &kbase_csf_fw_core_dump_fops);
#endif /* CONFIG_DEBUG_FS */
}

int kbase_csf_firmware_core_dump_entry_parse(struct kbase_device *kbdev, const u32 *entry)
{
	/* Casting to u16 as version is defined by bits 15:0 */
	kbdev->csf.fw_core_dump.version = (u16)entry[FW_CORE_DUMP_VERSION_INDEX];

	if (kbdev->csf.fw_core_dump.version != FW_CORE_DUMP_DATA_VERSION)
		return -EPERM;

	kbdev->csf.fw_core_dump.mcu_regs_addr = entry[FW_CORE_DUMP_START_ADDR_INDEX];
	kbdev->csf.fw_core_dump.available = true;

	return 0;
}
