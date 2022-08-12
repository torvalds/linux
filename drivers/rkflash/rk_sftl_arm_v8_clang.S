/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018-2021 Rockchip Electronics Co. Ltd.
 * date: 2022-08-15
 */
	.text
	.section	.note.gnu.property,"a",@note
	.p2align	3
	.word	4
	.word	16
	.word	5
	.asciz	"GNU"
	.word	3221225472
	.word	4
	.word	3
	.word	0
.Lsec_end0:
	.text
	.file	"rk_sftl.c"
	.globl	ftl_print_sblk_info             // -- Begin function ftl_print_sblk_info
	.p2align	2
	.type	ftl_print_sblk_info,@function
ftl_print_sblk_info:                    // @ftl_print_sblk_info
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	adrp	x0, .L.str
	adrp	x1, .L.str.1
	add	x0, x0, :lo12:.L.str
	add	x1, x1, :lo12:.L.str.1
	str	x19, [sp, #16]                  // 8-byte Folded Spill
	mov	x29, sp
	bl	sftl_printk
	adrp	x8, g_active_superblock
	add	x8, x8, :lo12:g_active_superblock
	adrp	x19, p_valid_page_count_table
	ldrh	w1, [x8]
	ldr	x9, [x19, :lo12:p_valid_page_count_table]
	ldrh	w2, [x8, #2]
	ldrb	w3, [x8, #6]
	ldrb	w4, [x8, #8]
	ldrh	w5, [x8, #4]
	ldrh	w6, [x9, x1, lsl #1]
	adrp	x0, .L.str.2
	add	x0, x0, :lo12:.L.str.2
                                        // kill: def $w1 killed $w1 killed $x1
	bl	sftl_printk
	adrp	x8, g_buffer_superblock
	add	x8, x8, :lo12:g_buffer_superblock
	ldrh	w1, [x8]
	ldr	x9, [x19, :lo12:p_valid_page_count_table]
	ldrh	w2, [x8, #2]
	ldrb	w3, [x8, #6]
	ldrb	w4, [x8, #8]
	ldrh	w5, [x8, #4]
	ldrh	w6, [x9, x1, lsl #1]
	adrp	x0, .L.str.3
	add	x0, x0, :lo12:.L.str.3
                                        // kill: def $w1 killed $w1 killed $x1
	bl	sftl_printk
	adrp	x8, g_gc_temp_superblock
	add	x8, x8, :lo12:g_gc_temp_superblock
	ldrh	w1, [x8]
	ldr	x9, [x19, :lo12:p_valid_page_count_table]
	ldrh	w2, [x8, #2]
	ldrb	w3, [x8, #6]
	ldrb	w4, [x8, #8]
	ldrh	w5, [x8, #4]
	ldrh	w6, [x9, x1, lsl #1]
	adrp	x0, .L.str.4
	add	x0, x0, :lo12:.L.str.4
                                        // kill: def $w1 killed $w1 killed $x1
	bl	sftl_printk
	adrp	x8, g_gc_superblock
	add	x8, x8, :lo12:g_gc_superblock
	ldrh	w1, [x8]
	ldr	x9, [x19, :lo12:p_valid_page_count_table]
	ldrh	w2, [x8, #2]
	ldrb	w3, [x8, #6]
	ldrb	w4, [x8, #8]
	ldrh	w5, [x8, #4]
	ldrh	w6, [x9, x1, lsl #1]
	adrp	x0, .L.str.5
	add	x0, x0, :lo12:.L.str.5
                                        // kill: def $w1 killed $w1 killed $x1
	bl	sftl_printk
	adrp	x8, g_num_free_superblocks
	adrp	x9, g_gc_next_blk
	adrp	x10, g_gc_next_blk_1
	ldrh	w1, [x8, :lo12:g_num_free_superblocks]
	ldrh	w2, [x9, :lo12:g_gc_next_blk]
	ldrh	w3, [x10, :lo12:g_gc_next_blk_1]
	adrp	x0, .L.str.6
	add	x0, x0, :lo12:.L.str.6
	bl	sftl_printk
	ldr	x19, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end0:
	.size	ftl_print_sblk_info, .Lfunc_end0-ftl_print_sblk_info
                                        // -- End function
	.globl	FtlPrintInfo2buf                // -- Begin function FtlPrintInfo2buf
	.p2align	2
	.type	FtlPrintInfo2buf,@function
FtlPrintInfo2buf:                       // @FtlPrintInfo2buf
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-96]!           // 16-byte Folded Spill
	adrp	x8, .L.str.7
	add	x8, x8, :lo12:.L.str.7
	ldur	x9, [x8, #5]
	ldr	x8, [x8]
	stp	x20, x19, [sp, #80]             // 16-byte Folded Spill
	add	x20, x0, #12                    // =12
	stur	x9, [x0, #5]
	str	x8, [x0]
	adrp	x8, DeviceCapacity
	ldr	w3, [x8, :lo12:DeviceCapacity]
	adrp	x2, .L.str.8
	mov	x19, x0
	add	x2, x2, :lo12:.L.str.8
	mov	w1, #64
	mov	x0, x20
	stp	x28, x27, [sp, #16]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #32]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #48]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #64]             // 16-byte Folded Spill
	mov	x29, sp
	bl	snprintf
	adrp	x8, .L.str.9
	add	x8, x8, :lo12:.L.str.9
	ldr	x8, [x8]
	mov	w10, #14927
	add	x9, x20, w0, sxtw
	movk	w10, #10, lsl #16
	str	x8, [x9]
	stur	w10, [x9, #7]
	adrp	x8, g_MaxLpn
	ldr	w3, [x8, :lo12:g_MaxLpn]
	add	x20, x9, #10                    // =10
	adrp	x2, .L.str.10
	add	x2, x2, :lo12:.L.str.10
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_VaildLpn
	ldr	w3, [x8, :lo12:g_VaildLpn]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.11
	add	x2, x2, :lo12:.L.str.11
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_totle_read_page_count
	ldr	w3, [x8, :lo12:g_totle_read_page_count]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.12
	add	x2, x2, :lo12:.L.str.12
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_totle_discard_page_count
	ldr	w3, [x8, :lo12:g_totle_discard_page_count]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.13
	add	x2, x2, :lo12:.L.str.13
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_totle_write_page_count
	ldr	w3, [x8, :lo12:g_totle_write_page_count]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.14
	add	x2, x2, :lo12:.L.str.14
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_totle_cache_write_count
	ldr	w3, [x8, :lo12:g_totle_cache_write_count]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.15
	add	x2, x2, :lo12:.L.str.15
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_totle_l2p_write_count
	ldr	w3, [x8, :lo12:g_totle_l2p_write_count]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.16
	add	x2, x2, :lo12:.L.str.16
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_totle_gc_page_count
	ldr	w3, [x8, :lo12:g_totle_gc_page_count]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.17
	add	x2, x2, :lo12:.L.str.17
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_totle_write_sector
	ldr	w8, [x8, :lo12:g_totle_write_sector]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.18
	add	x2, x2, :lo12:.L.str.18
	lsr	w3, w8, #11
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_totle_read_sector
	ldr	w8, [x8, :lo12:g_totle_read_sector]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.19
	add	x2, x2, :lo12:.L.str.19
	lsr	w3, w8, #11
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_GlobalSysVersion
	ldr	w3, [x8, :lo12:g_GlobalSysVersion]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.20
	add	x2, x2, :lo12:.L.str.20
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_GlobalDataVersion
	ldr	w3, [x8, :lo12:g_GlobalDataVersion]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.21
	add	x2, x2, :lo12:.L.str.21
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x22, gBbtInfo
	add	x22, x22, :lo12:gBbtInfo
	ldrh	w3, [x22, #6]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.22
	add	x2, x2, :lo12:.L.str.22
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x21, g_num_free_superblocks
	ldrh	w3, [x21, :lo12:g_num_free_superblocks]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.23
	add	x2, x2, :lo12:.L.str.23
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_totle_mlc_erase_count
	ldr	w3, [x8, :lo12:g_totle_mlc_erase_count]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.24
	add	x2, x2, :lo12:.L.str.24
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_totle_slc_erase_count
	ldr	w3, [x8, :lo12:g_totle_slc_erase_count]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.25
	add	x2, x2, :lo12:.L.str.25
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_totle_avg_erase_count
	ldr	w3, [x8, :lo12:g_totle_avg_erase_count]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.26
	add	x2, x2, :lo12:.L.str.26
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_totle_sys_slc_erase_count
	ldr	w3, [x8, :lo12:g_totle_sys_slc_erase_count]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.27
	add	x2, x2, :lo12:.L.str.27
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_max_erase_count
	ldr	w3, [x8, :lo12:g_max_erase_count]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.28
	add	x2, x2, :lo12:.L.str.28
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_min_erase_count
	ldr	w3, [x8, :lo12:g_min_erase_count]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.29
	add	x2, x2, :lo12:.L.str.29
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x23, g_sys_save_data+28
	add	x23, x23, :lo12:g_sys_save_data+28
	ldrh	w3, [x23, #2]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.30
	add	x2, x2, :lo12:.L.str.30
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrh	w3, [x23]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.31
	add	x2, x2, :lo12:.L.str.31
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_MaxLbaSector
	ldr	w3, [x8, :lo12:g_MaxLbaSector]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.32
	add	x2, x2, :lo12:.L.str.32
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, c_ftl_nand_init_sys_blks_per_plane
	ldr	w3, [x8, :lo12:c_ftl_nand_init_sys_blks_per_plane]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.33
	add	x2, x2, :lo12:.L.str.33
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, c_ftl_nand_sys_blks_per_plane
	ldr	w3, [x8, :lo12:c_ftl_nand_sys_blks_per_plane]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.34
	add	x2, x2, :lo12:.L.str.34
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, gSysFreeQueue+6
	ldrh	w3, [x8, :lo12:gSysFreeQueue+6]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.35
	add	x2, x2, :lo12:.L.str.35
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, c_ftl_nand_data_blks_per_plane
	ldrh	w3, [x8, :lo12:c_ftl_nand_data_blks_per_plane]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.36
	add	x2, x2, :lo12:.L.str.36
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, c_ftl_nand_data_op_blks_per_plane
	ldrh	w3, [x8, :lo12:c_ftl_nand_data_op_blks_per_plane]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.37
	add	x2, x2, :lo12:.L.str.37
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, c_ftl_nand_max_data_blks
	ldr	w3, [x8, :lo12:c_ftl_nand_max_data_blks]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.38
	add	x2, x2, :lo12:.L.str.38
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, gSysInfo
	ldrh	w3, [x8, :lo12:gSysInfo]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.39
	add	x2, x2, :lo12:.L.str.39
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrh	w3, [x22]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.40
	add	x2, x2, :lo12:.L.str.40
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x22, g_active_superblock
	add	x22, x22, :lo12:g_active_superblock
	ldrh	w3, [x22, #2]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.41
	add	x2, x2, :lo12:.L.str.41
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrb	w3, [x22, #6]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.42
	add	x2, x2, :lo12:.L.str.42
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrh	w3, [x22]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.43
	add	x2, x2, :lo12:.L.str.43
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrb	w3, [x22, #8]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.44
	add	x2, x2, :lo12:.L.str.44
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrh	w3, [x22, #4]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.45
	add	x2, x2, :lo12:.L.str.45
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x23, p_valid_page_count_table
	ldr	x8, [x23, :lo12:p_valid_page_count_table]
	ldrh	w9, [x22]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.46
	add	x2, x2, :lo12:.L.str.46
	ldrh	w3, [x8, x9, lsl #1]
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x22, g_buffer_superblock
	add	x22, x22, :lo12:g_buffer_superblock
	ldrh	w3, [x22, #2]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.47
	add	x2, x2, :lo12:.L.str.47
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrb	w3, [x22, #6]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.48
	add	x2, x2, :lo12:.L.str.48
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrh	w3, [x22]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.49
	add	x2, x2, :lo12:.L.str.49
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrb	w3, [x22, #8]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.50
	add	x2, x2, :lo12:.L.str.50
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrh	w3, [x22, #4]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.51
	add	x2, x2, :lo12:.L.str.51
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldr	x8, [x23, :lo12:p_valid_page_count_table]
	ldrh	w9, [x22]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.52
	add	x2, x2, :lo12:.L.str.52
	ldrh	w3, [x8, x9, lsl #1]
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x22, g_gc_temp_superblock
	add	x22, x22, :lo12:g_gc_temp_superblock
	ldrh	w3, [x22, #2]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.53
	add	x2, x2, :lo12:.L.str.53
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrb	w3, [x22, #6]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.54
	add	x2, x2, :lo12:.L.str.54
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrh	w3, [x22]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.55
	add	x2, x2, :lo12:.L.str.55
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrb	w3, [x22, #8]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.56
	add	x2, x2, :lo12:.L.str.56
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrh	w3, [x22, #4]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.57
	add	x2, x2, :lo12:.L.str.57
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x22, g_gc_superblock
	add	x22, x22, :lo12:g_gc_superblock
	ldrh	w3, [x22, #2]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.58
	add	x2, x2, :lo12:.L.str.58
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrb	w3, [x22, #6]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.59
	add	x2, x2, :lo12:.L.str.59
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrh	w3, [x22]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.60
	add	x2, x2, :lo12:.L.str.60
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrb	w3, [x22, #8]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.61
	add	x2, x2, :lo12:.L.str.61
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldrh	w3, [x22, #4]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.62
	add	x2, x2, :lo12:.L.str.62
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x22, g_sys_ext_data+72
	add	x22, x22, :lo12:g_sys_ext_data+72
	ldp	w3, w4, [x22, #8]
	ldr	w5, [x22, #4]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.63
	add	x2, x2, :lo12:.L.str.63
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldr	w3, [x22]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.64
	add	x2, x2, :lo12:.L.str.64
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldr	w3, [x22, #24]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.65
	add	x2, x2, :lo12:.L.str.65
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_gc_free_blk_threshold
	ldrh	w3, [x8, :lo12:g_gc_free_blk_threshold]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.66
	add	x2, x2, :lo12:.L.str.66
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_gc_merge_free_blk_threshold
	ldrh	w3, [x8, :lo12:g_gc_merge_free_blk_threshold]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.67
	add	x2, x2, :lo12:.L.str.67
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_gc_skip_write_count
	ldr	w3, [x8, :lo12:g_gc_skip_write_count]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.68
	add	x2, x2, :lo12:.L.str.68
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, g_gc_blk_index
	ldrh	w3, [x8, :lo12:g_gc_blk_index]
	add	x20, x20, w0, sxtw
	adrp	x2, .L.str.69
	add	x2, x2, :lo12:.L.str.69
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x26, p_free_data_block_list_head
	ldr	x8, [x26, :lo12:p_free_data_block_list_head]
	add	x20, x20, w0, sxtw
	adrp	x24, p_data_block_list_table
	adrp	x25, p_erase_count_table
	cbz	x8, .LBB1_2
// %bb.1:
	ldr	w9, [x24, :lo12:p_data_block_list_table]
	ldr	x10, [x25, :lo12:p_erase_count_table]
	sub	w8, w8, w9
	mov	w9, #43691
	lsr	w8, w8, #1
	movk	w9, #43690, lsl #16
	mul	w8, w8, w9
	and	x8, x8, #0xffff
	ldrh	w3, [x10, x8, lsl #1]
	b	.LBB1_3
.LBB1_2:
	mov	w3, wzr
.LBB1_3:
	adrp	x2, .L.str.70
	add	x2, x2, :lo12:.L.str.70
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	ldr	x9, [x26, :lo12:p_free_data_block_list_head]
	add	x20, x20, w0, sxtw
	cbz	x9, .LBB1_8
// %bb.4:
	ldrh	w10, [x21, :lo12:g_num_free_superblocks]
	ldr	x8, [x24, :lo12:p_data_block_list_table]
	lsl	w11, w10, #3
	sub	x9, x9, x8
	sub	w11, w11, w10
	lsr	x12, x9, #1
	lsr	w9, w11, #3
	cmp	w9, w10
	csel	w9, w9, w10, lo
	mov	w10, #43691
	movk	w10, #43690, lsl #16
	mul	w12, w12, w10
	cbz	w9, .LBB1_9
// %bb.5:
	mov	w10, wzr
	mov	w11, #6
	mov	w13, #65535
.LBB1_6:                                // =>This Inner Loop Header: Depth=1
	and	x14, x12, #0xffff
	mul	x14, x14, x11
	ldrh	w14, [x8, x14]
	mov	w15, w12
	cmp	w14, w13
	b.eq	.LBB1_10
// %bb.7:                               //   in Loop: Header=BB1_6 Depth=1
	add	w10, w10, #1                    // =1
	and	w12, w10, #0xffff
	cmp	w12, w9, uxth
	mov	w12, w14
	mov	w15, w14
	b.lo	.LBB1_6
	b	.LBB1_10
.LBB1_8:
	mov	w3, wzr
	b	.LBB1_11
.LBB1_9:
	mov	w15, w12
.LBB1_10:
	ldr	x8, [x25, :lo12:p_erase_count_table]
	and	x9, x15, #0xffff
	ldrh	w3, [x8, x9, lsl #1]
.LBB1_11:
	adrp	x2, .L.str.71
	add	x2, x2, :lo12:.L.str.71
	mov	w1, #64
	mov	x0, x20
	bl	snprintf
	adrp	x8, gFtlInitStatus
	ldr	w8, [x8, :lo12:gFtlInitStatus]
	add	x20, x20, w0, sxtw
	cmp	w8, #1                          // =1
	b.ne	.LBB1_24
// %bb.12:
	adrp	x8, g_gc_superblock
	ldrh	w8, [x8, :lo12:g_gc_superblock]
	mov	w9, #65535
	cmp	x8, x9
	b.eq	.LBB1_14
// %bb.13:
	ldr	x9, [x23, :lo12:p_valid_page_count_table]
	adrp	x2, .L.str.72
	add	x2, x2, :lo12:.L.str.72
	mov	w1, #64
	ldrh	w3, [x9, x8, lsl #1]
	mov	x0, x20
	bl	snprintf
	add	x20, x20, w0, sxtw
.LBB1_14:
	adrp	x8, p_data_block_list_head
	ldr	x8, [x8, :lo12:p_data_block_list_head]
	cbz	x8, .LBB1_19
// %bb.15:
	ldr	x9, [x24, :lo12:p_data_block_list_table]
	sub	x8, x8, x9
	mov	w9, #43691
	lsr	x8, x8, #1
	movk	w9, #43690, lsl #16
	mul	w9, w8, w9
	mvn	w8, w9
	tst	w8, #0xffff
	b.eq	.LBB1_19
// %bb.16:
	ldr	x8, [x24, :lo12:p_data_block_list_table]
	adrp	x22, .L.str.73
	mov	w21, wzr
	add	x22, x22, :lo12:.L.str.73
	mov	w27, #65535
.LBB1_17:                               // =>This Inner Loop Header: Depth=1
	and	x11, x9, #0xffff
	ldr	x10, [x23, :lo12:p_valid_page_count_table]
	ldr	x12, [x25, :lo12:p_erase_count_table]
	lsl	x13, x11, #1
	add	x11, x13, x11
	lsl	x28, x11, #1
	add	x8, x8, x28
	ldrh	w5, [x10, x13]
	ldrh	w6, [x8, #4]
	ldrh	w7, [x12, x13]
	and	w4, w9, #0xffff
	mov	w1, #64
	mov	x0, x20
	mov	x2, x22
	mov	w3, w21
	bl	snprintf
	ldr	x8, [x24, :lo12:p_data_block_list_table]
	cmp	w21, #14                        // =14
	add	x20, x20, w0, sxtw
	b.hi	.LBB1_20
// %bb.18:                              //   in Loop: Header=BB1_17 Depth=1
	ldrh	w9, [x8, x28]
	add	w21, w21, #1                    // =1
	cmp	w9, w27
	b.ne	.LBB1_17
	b	.LBB1_20
.LBB1_19:
	ldr	x8, [x24, :lo12:p_data_block_list_table]
.LBB1_20:
	ldr	x9, [x26, :lo12:p_free_data_block_list_head]
	mov	w10, #43691
	movk	w10, #43690, lsl #16
	sub	x9, x9, x8
	lsr	x9, x9, #1
	mul	w9, w9, w10
	mvn	w10, w9
	tst	w10, #0xffff
	b.eq	.LBB1_24
// %bb.21:
	adrp	x22, .L.str.74
	mov	w21, wzr
	add	x22, x22, :lo12:.L.str.74
	mov	w23, #65535
.LBB1_22:                               // =>This Inner Loop Header: Depth=1
	and	x10, x9, #0xffff
	ldr	x11, [x25, :lo12:p_erase_count_table]
	lsl	x12, x10, #1
	add	x10, x12, x10
	lsl	x26, x10, #1
	add	x8, x8, x26
	ldrh	w5, [x8, #4]
	ldrh	w6, [x11, x12]
	and	w4, w9, #0xffff
	mov	w1, #64
	mov	x0, x20
	mov	x2, x22
	mov	w3, w21
	bl	snprintf
	cmp	w21, #2                         // =2
	add	x20, x20, w0, sxtw
	b.hi	.LBB1_24
// %bb.23:                              //   in Loop: Header=BB1_22 Depth=1
	ldr	x8, [x24, :lo12:p_data_block_list_table]
	add	w21, w21, #1                    // =1
	ldrh	w9, [x8, x26]
	cmp	w9, w23
	b.ne	.LBB1_22
.LBB1_24:
	sub	w0, w20, w19
	ldp	x20, x19, [sp, #80]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #64]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #48]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #32]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #16]             // 16-byte Folded Reload
	ldp	x29, x30, [sp], #96             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end1:
	.size	FtlPrintInfo2buf, .Lfunc_end1-FtlPrintInfo2buf
                                        // -- End function
	.globl	GetFreeBlockMinEraseCount       // -- Begin function GetFreeBlockMinEraseCount
	.p2align	2
	.type	GetFreeBlockMinEraseCount,@function
GetFreeBlockMinEraseCount:              // @GetFreeBlockMinEraseCount
// %bb.0:
	hint	#25
	adrp	x8, p_free_data_block_list_head
	ldr	x8, [x8, :lo12:p_free_data_block_list_head]
	cbz	x8, .LBB2_2
// %bb.1:
	adrp	x9, p_data_block_list_table
	ldr	w9, [x9, :lo12:p_data_block_list_table]
	adrp	x10, p_erase_count_table
	ldr	x10, [x10, :lo12:p_erase_count_table]
	sub	w8, w8, w9
	mov	w9, #43691
	lsr	w8, w8, #1
	movk	w9, #43690, lsl #16
	mul	w8, w8, w9
	and	x8, x8, #0xffff
	ldrh	w0, [x10, x8, lsl #1]
	hint	#29
	ret
.LBB2_2:
	mov	w0, wzr
	hint	#29
	ret
.Lfunc_end2:
	.size	GetFreeBlockMinEraseCount, .Lfunc_end2-GetFreeBlockMinEraseCount
                                        // -- End function
	.globl	GetFreeBlockMaxEraseCount       // -- Begin function GetFreeBlockMaxEraseCount
	.p2align	2
	.type	GetFreeBlockMaxEraseCount,@function
GetFreeBlockMaxEraseCount:              // @GetFreeBlockMaxEraseCount
// %bb.0:
	hint	#25
	adrp	x8, p_free_data_block_list_head
	ldr	x9, [x8, :lo12:p_free_data_block_list_head]
	cbz	x9, .LBB3_5
// %bb.1:
	adrp	x8, g_num_free_superblocks
	adrp	x10, p_data_block_list_table
	ldrh	w11, [x8, :lo12:g_num_free_superblocks]
	ldr	x8, [x10, :lo12:p_data_block_list_table]
	mov	w12, #43691
	movk	w12, #43690, lsl #16
	lsl	w10, w11, #3
	sub	x9, x9, x8
	sub	w10, w10, w11
	lsr	x11, x9, #1
	lsr	w9, w10, #3
	cmp	w9, w0, uxth
	csel	w9, w9, w0, lo
	and	w10, w9, #0xffff
	mul	w12, w11, w12
	cbz	w10, .LBB3_6
// %bb.2:
	mov	w10, wzr
	mov	w11, #6
	mov	w13, #65535
.LBB3_3:                                // =>This Inner Loop Header: Depth=1
	and	x14, x12, #0xffff
	mul	x14, x14, x11
	ldrh	w14, [x8, x14]
	mov	w15, w12
	cmp	w14, w13
	b.eq	.LBB3_7
// %bb.4:                               //   in Loop: Header=BB3_3 Depth=1
	add	w10, w10, #1                    // =1
	and	w12, w10, #0xffff
	cmp	w12, w9, uxth
	mov	w12, w14
	mov	w15, w14
	b.lo	.LBB3_3
	b	.LBB3_7
.LBB3_5:
	mov	w0, wzr
	hint	#29
	ret
.LBB3_6:
	mov	w15, w12
.LBB3_7:
	adrp	x8, p_erase_count_table
	ldr	x8, [x8, :lo12:p_erase_count_table]
	and	x9, x15, #0xffff
	ldrh	w0, [x8, x9, lsl #1]
	hint	#29
	ret
.Lfunc_end3:
	.size	GetFreeBlockMaxEraseCount, .Lfunc_end3-GetFreeBlockMaxEraseCount
                                        // -- End function
	.globl	List_get_gc_head_node           // -- Begin function List_get_gc_head_node
	.p2align	2
	.type	List_get_gc_head_node,@function
List_get_gc_head_node:                  // @List_get_gc_head_node
// %bb.0:
	hint	#25
	adrp	x8, p_data_block_list_head
	ldr	x8, [x8, :lo12:p_data_block_list_head]
	cbz	x8, .LBB4_6
// %bb.1:
	adrp	x9, p_data_block_list_table
	ldr	x9, [x9, :lo12:p_data_block_list_table]
	tst	w0, #0xffff
	b.eq	.LBB4_5
// %bb.2:
	mov	w10, #6
.LBB4_3:                                // =>This Inner Loop Header: Depth=1
	ldrh	w11, [x8]
	mov	w8, #65535
	cmp	x11, x8
	b.eq	.LBB4_7
// %bb.4:                               //   in Loop: Header=BB4_3 Depth=1
	sub	w0, w0, #1                      // =1
	tst	w0, #0xffff
	madd	x8, x11, x10, x9
	b.ne	.LBB4_3
.LBB4_5:
	sub	w8, w8, w9
	mov	w9, #43691
	lsr	w8, w8, #1
	movk	w9, #43690, lsl #16
	mul	w8, w8, w9
	and	w8, w8, #0xffff
	b	.LBB4_7
.LBB4_6:
	mov	w8, #65535
.LBB4_7:
	mov	w0, w8
	hint	#29
	ret
.Lfunc_end4:
	.size	List_get_gc_head_node, .Lfunc_end4-List_get_gc_head_node
                                        // -- End function
	.globl	rknand_proc_ftlread             // -- Begin function rknand_proc_ftlread
	.p2align	2
	.type	rknand_proc_ftlread,@function
rknand_proc_ftlread:                    // @rknand_proc_ftlread
// %bb.0:
	hint	#34
	cmp	w0, #2048                       // =2048
	b.ge	.LBB5_2
// %bb.1:
	mov	w0, wzr
	ret
.LBB5_2:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #16]             // 16-byte Folded Spill
	mov	x19, x1
	adrp	x2, .L.str.75
	adrp	x3, .L.str.1
	add	x2, x2, :lo12:.L.str.75
	add	x3, x3, :lo12:.L.str.1
	mov	w1, #64
	mov	x0, x19
	mov	x29, sp
	bl	snprintf
	add	x20, x19, w0, sxtw
	mov	x0, x20
	bl	FtlPrintInfo2buf
	add	w8, w20, w0
	sub	w0, w8, w19
	ldp	x20, x19, [sp, #16]             // 16-byte Folded Reload
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end5:
	.size	rknand_proc_ftlread, .Lfunc_end5-rknand_proc_ftlread
                                        // -- End function
	.globl	Ftl_log2                        // -- Begin function Ftl_log2
	.p2align	2
	.type	Ftl_log2,@function
Ftl_log2:                               // @Ftl_log2
// %bb.0:
	hint	#25
	cbz	w0, .LBB6_3
// %bb.1:
	mov	w10, wzr
	mov	w9, #1
.LBB6_2:                                // =>This Inner Loop Header: Depth=1
	lsl	w9, w9, #1
	mov	w8, w10
	cmp	w9, w0
	add	w10, w10, #1                    // =1
	b.ls	.LBB6_2
	b	.LBB6_4
.LBB6_3:
	mov	w8, #65535
.LBB6_4:
	mov	w0, w8
	hint	#29
	ret
.Lfunc_end6:
	.size	Ftl_log2, .Lfunc_end6-Ftl_log2
                                        // -- End function
	.globl	FtlPrintInfo                    // -- Begin function FtlPrintInfo
	.p2align	2
	.type	FtlPrintInfo,@function
FtlPrintInfo:                           // @FtlPrintInfo
// %bb.0:
	hint	#25
	hint	#29
	ret
.Lfunc_end7:
	.size	FtlPrintInfo, .Lfunc_end7-FtlPrintInfo
                                        // -- End function
	.globl	FtlSysBlkNumInit                // -- Begin function FtlSysBlkNumInit
	.p2align	2
	.type	FtlSysBlkNumInit,@function
FtlSysBlkNumInit:                       // @FtlSysBlkNumInit
// %bb.0:
	hint	#25
	and	w8, w0, #0xffff
	adrp	x9, c_ftl_nand_blk_pre_plane
	cmp	w8, #24                         // =24
	ldrh	w8, [x9, :lo12:c_ftl_nand_blk_pre_plane]
	mov	w9, #24
	csel	w9, w0, w9, hi
	adrp	x10, c_ftl_nand_data_blks_per_plane
	sub	w8, w8, w9
	strh	w8, [x10, :lo12:c_ftl_nand_data_blks_per_plane]
	adrp	x8, c_ftl_nand_sys_blks_per_plane
	adrp	x10, c_ftl_nand_planes_num
	and	w9, w9, #0xffff
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_num]
	str	w9, [x8, :lo12:c_ftl_nand_sys_blks_per_plane]
	adrp	x8, c_ftl_nand_totle_phy_blks
	ldr	w8, [x8, :lo12:c_ftl_nand_totle_phy_blks]
	mul	w9, w10, w9
	adrp	x10, c_ftl_nand_max_sys_blks
	str	w9, [x10, :lo12:c_ftl_nand_max_sys_blks]
	sub	w8, w8, w9
	adrp	x9, c_ftl_nand_max_data_blks
	str	w8, [x9, :lo12:c_ftl_nand_max_data_blks]
	mov	w0, wzr
	hint	#29
	ret
.Lfunc_end8:
	.size	FtlSysBlkNumInit, .Lfunc_end8-FtlSysBlkNumInit
                                        // -- End function
	.globl	FtlConstantsInit                // -- Begin function FtlConstantsInit
	.p2align	2
	.type	FtlConstantsInit,@function
FtlConstantsInit:                       // @FtlConstantsInit
// %bb.0:
	hint	#25
	ldrh	w12, [x0]
	adrp	x8, c_ftl_nand_type
	adrp	x14, c_ftl_nand_planes_per_die
	adrp	x13, c_ftl_nand_blk_pre_plane
	strh	w12, [x8, :lo12:c_ftl_nand_type]
	ldrh	w10, [x0, #2]
	adrp	x8, c_ftl_nand_die_num
	adrp	x15, c_ftl_nand_sec_pre_page
	strh	w10, [x8, :lo12:c_ftl_nand_die_num]
	ldrh	w11, [x0, #4]
	strh	w11, [x14, :lo12:c_ftl_nand_planes_per_die]
	ldrh	w8, [x0, #6]
	strh	w8, [x13, :lo12:c_ftl_nand_blk_pre_plane]
	ldrh	w9, [x0, #14]
	cmp	w9, #8                          // =8
	strh	w9, [x15, :lo12:c_ftl_nand_sec_pre_page]
	b.eq	.LBB9_3
// %bb.1:
	cmp	w9, #4                          // =4
	b.ne	.LBB9_6
// %bb.2:
	lsr	w8, w8, #1
	mov	w9, #8
	strh	w8, [x13, :lo12:c_ftl_nand_blk_pre_plane]
	strh	w9, [x15, :lo12:c_ftl_nand_sec_pre_page]
	b	.LBB9_6
.LBB9_3:
	cmp	w11, #1                         // =1
	b.ne	.LBB9_5
// %bb.4:
	lsr	w8, w8, #1
	mov	w11, #2
	strh	w8, [x13, :lo12:c_ftl_nand_blk_pre_plane]
	strh	w11, [x14, :lo12:c_ftl_nand_planes_per_die]
.LBB9_5:
	mov	w9, #8
.LBB9_6:
	mov	x15, #256
	mov	x16, #2312
	mov	x17, #4368
	mov	x18, #6424
	movk	x15, #770, lsl #16
	movk	x16, #2826, lsl #16
	movk	x17, #4882, lsl #16
	movk	x18, #6938, lsl #16
	adrp	x14, p_plane_order_table
	movk	x15, #1284, lsl #32
	movk	x16, #3340, lsl #32
	movk	x17, #5396, lsl #32
	movk	x18, #7452, lsl #32
	add	x14, x14, :lo12:p_plane_order_table
	movk	x15, #1798, lsl #48
	movk	x16, #3854, lsl #48
	movk	x17, #5910, lsl #48
	movk	x18, #7966, lsl #48
	adrp	x13, c_mlc_erase_count_value
	mov	w1, #5
	adrp	x2, c_ftl_nand_ext_blk_pre_plane
	cmp	w12, #1                         // =1
	stp	x15, x16, [x14]
	stp	x17, x18, [x14, #16]
	strh	w1, [x13, :lo12:c_mlc_erase_count_value]
	strh	wzr, [x2, :lo12:c_ftl_nand_ext_blk_pre_plane]
	b.ne	.LBB9_8
// %bb.7:
	strh	w12, [x13, :lo12:c_mlc_erase_count_value]
.LBB9_8:
	mul	w12, w11, w10
	adrp	x10, c_ftl_nand_planes_num
	mul	w11, w8, w11
	adrp	x13, c_ftl_vendor_part_size
	mov	w14, #640
	adrp	x15, c_ftl_nand_blks_per_die
	strh	w12, [x10, :lo12:c_ftl_nand_planes_num]
	ands	w10, w11, #0xffff
	strh	w14, [x13, :lo12:c_ftl_vendor_part_size]
	strh	w11, [x15, :lo12:c_ftl_nand_blks_per_die]
	b.eq	.LBB9_11
// %bb.9:
	mov	w14, wzr
	mov	w13, #1
.LBB9_10:                               // =>This Inner Loop Header: Depth=1
	lsl	w13, w13, #1
	mov	w11, w14
	cmp	w13, w10
	add	w14, w14, #1                    // =1
	b.ls	.LBB9_10
	b	.LBB9_12
.LBB9_11:
	mov	w11, #65535
.LBB9_12:
	adrp	x10, c_ftl_nand_blks_per_die_shift
	strh	w11, [x10, :lo12:c_ftl_nand_blks_per_die_shift]
	ldrh	w10, [x0, #12]
	adrp	x11, c_ftl_nand_page_pre_blk
	adrp	x13, c_ftl_nand_page_pre_slc_blk
	strh	w10, [x11, :lo12:c_ftl_nand_page_pre_blk]
	strh	w10, [x13, :lo12:c_ftl_nand_page_pre_slc_blk]
	mul	w11, w10, w12
	adrp	x13, c_ftl_nand_page_pre_super_blk
	strh	w11, [x13, :lo12:c_ftl_nand_page_pre_super_blk]
	cbz	w9, .LBB9_15
// %bb.13:
	mov	w14, wzr
	mov	w13, #1
.LBB9_14:                               // =>This Inner Loop Header: Depth=1
	lsl	w13, w13, #1
	mov	w11, w14
	cmp	w13, w9
	add	w14, w14, #1                    // =1
	b.ls	.LBB9_14
	b	.LBB9_16
.LBB9_15:
	mov	w11, #65535
.LBB9_16:
	adrp	x14, c_ftl_nand_sec_pre_page_shift
	lsl	w13, w9, #9
	adrp	x15, c_ftl_nand_byte_pre_page
	ubfiz	w16, w9, #1, #7
	adrp	x17, c_ftl_nand_byte_pre_oob
	strh	w13, [x15, :lo12:c_ftl_nand_byte_pre_page]
	strh	w16, [x17, :lo12:c_ftl_nand_byte_pre_oob]
	adrp	x17, c_ftl_nand_l2pmap_ram_region_num
	strh	w11, [x14, :lo12:c_ftl_nand_sec_pre_page_shift]
	mov	w14, #32
	ldrh	w18, [x0, #20]
	strh	w14, [x17, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	mov	w14, #640
	lsr	w14, w14, w11
	and	w12, w12, #0xffff
	adrp	x17, c_ftl_nand_vendor_region_num
	add	w14, w14, #2                    // =2
	mul	w9, w9, w10
	strh	w14, [x17, :lo12:c_ftl_nand_vendor_region_num]
	mul	w14, w8, w12
	mul	w17, w9, w14
	adrp	x16, DeviceCapacity
	asr	w17, w17, #11
	str	w17, [x16, :lo12:DeviceCapacity]
	mov	w16, #5120
	sdiv	w9, w16, w9
	and	w16, w9, #0xffff
	mov	w17, #4
	cmp	w16, #5                         // =5
	adrp	x15, c_ftl_nand_reserved_blks
	and	w11, w11, #0xffff
	csel	w9, w17, w9, lo
	strh	w18, [x15, :lo12:c_ftl_nand_reserved_blks]
	lsl	w15, w8, #6
	add	w17, w9, #3                     // =3
	add	w11, w11, #9                    // =9
	and	w9, w9, #0xffff
	lsr	w15, w15, w11
	udiv	w9, w9, w12
	adrp	x16, c_ftl_nand_max_vendor_blks
	add	w9, w15, w9
	strh	w17, [x16, :lo12:c_ftl_nand_max_vendor_blks]
	cmp	w12, #1                         // =1
	add	w17, w9, #8                     // =8
	add	w9, w9, #12                     // =12
	csel	w9, w9, w17, eq
	and	w17, w9, #0xffff
	mov	w16, #24
	cmp	w17, #24                        // =24
	adrp	x18, c_ftl_nand_map_blks_per_plane
	csel	w9, w9, w16, hi
	adrp	x16, c_ftl_nand_data_blks_per_plane
	sub	w8, w8, w9
	strh	w15, [x18, :lo12:c_ftl_nand_map_blks_per_plane]
	orr	w18, wzr, #0x3
	and	w9, w9, #0xffff
	strh	w8, [x16, :lo12:c_ftl_nand_data_blks_per_plane]
	adrp	x8, c_ftl_nand_max_sys_blks
	madd	w15, w15, w12, w18
	mul	w12, w9, w12
	adrp	x16, c_ftl_nand_max_data_blks
	str	w12, [x8, :lo12:c_ftl_nand_max_sys_blks]
	sub	w8, w14, w12
	str	w8, [x16, :lo12:c_ftl_nand_max_data_blks]
	mul	w8, w10, w8
	adrp	x13, c_ftl_nand_totle_phy_blks
	lsl	w8, w8, #2
	adrp	x17, c_ftl_nand_sys_blks_per_plane
	str	w14, [x13, :lo12:c_ftl_nand_totle_phy_blks]
	adrp	x13, g_MaxLbaSector
	lsr	w8, w8, w11
	str	wzr, [x13, :lo12:g_MaxLbaSector]
	adrp	x13, c_ftl_nand_init_sys_blks_per_plane
	adrp	x18, c_ftl_nand_map_region_num
	str	w9, [x17, :lo12:c_ftl_nand_sys_blks_per_plane]
	adrp	x17, c_ftl_nand_max_map_blks
	add	w8, w8, #2                      // =2
	str	w9, [x13, :lo12:c_ftl_nand_init_sys_blks_per_plane]
	strh	w8, [x18, :lo12:c_ftl_nand_map_region_num]
	str	w15, [x17, :lo12:c_ftl_nand_max_map_blks]
	mov	w0, wzr
	hint	#29
	ret
.Lfunc_end9:
	.size	FtlConstantsInit, .Lfunc_end9-FtlConstantsInit
                                        // -- End function
	.globl	FtlMemInit                      // -- Begin function FtlMemInit
	.p2align	2
	.type	FtlMemInit,@function
FtlMemInit:                             // @FtlMemInit
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-96]!           // 16-byte Folded Spill
	adrp	x9, g_gc_head_data_block
	mov	w10, #65535
	adrp	x8, g_GlobalSysVersion
	str	w10, [x9, :lo12:g_gc_head_data_block]
	adrp	x9, g_gc_next_blk
	strh	w10, [x9, :lo12:g_gc_next_blk]
	adrp	x9, g_gc_next_blk_1
	str	wzr, [x8, :lo12:g_GlobalSysVersion]
	adrp	x8, g_totle_write_page_count
	strh	w10, [x9, :lo12:g_gc_next_blk_1]
	adrp	x9, g_gc_free_blk_threshold
	mov	w10, #32
	str	wzr, [x8, :lo12:g_totle_write_page_count]
	adrp	x8, g_totle_l2p_write_count
	strh	w10, [x9, :lo12:g_gc_free_blk_threshold]
	adrp	x9, g_gc_merge_free_blk_threshold
	mov	w10, #128
	str	wzr, [x8, :lo12:g_totle_l2p_write_count]
	adrp	x8, g_totle_slc_erase_count
	strh	w10, [x9, :lo12:g_gc_merge_free_blk_threshold]
	adrp	x9, g_GlobalDataVersion
	str	wzr, [x8, :lo12:g_totle_slc_erase_count]
	adrp	x8, g_min_erase_count
	str	wzr, [x9, :lo12:g_GlobalDataVersion]
	adrp	x9, g_totle_discard_page_count
	str	wzr, [x8, :lo12:g_min_erase_count]
	adrp	x8, g_gc_head_data_block_count
	stp	x20, x19, [sp, #80]             // 16-byte Folded Spill
	adrp	x10, g_totle_gc_page_count
	str	wzr, [x9, :lo12:g_totle_discard_page_count]
	adrp	x9, g_totle_read_page_count
	str	wzr, [x8, :lo12:g_gc_head_data_block_count]
	adrp	x8, g_gc_blk_index
	adrp	x19, c_ftl_nand_page_pre_super_blk
	str	wzr, [x10, :lo12:g_totle_gc_page_count]
	adrp	x10, g_totle_cache_write_count
	str	wzr, [x9, :lo12:g_totle_read_page_count]
	adrp	x9, g_totle_sys_slc_erase_count
	strh	wzr, [x8, :lo12:g_gc_blk_index]
	ldrh	w8, [x19, :lo12:c_ftl_nand_page_pre_super_blk]
	str	wzr, [x10, :lo12:g_totle_cache_write_count]
	adrp	x10, g_totle_mlc_erase_count
	str	wzr, [x9, :lo12:g_totle_sys_slc_erase_count]
	adrp	x9, g_in_gc_progress
	str	wzr, [x10, :lo12:g_totle_mlc_erase_count]
	adrp	x10, g_max_erase_count
	str	wzr, [x9, :lo12:g_in_gc_progress]
	adrp	x9, g_gc_skip_write_count
	str	wzr, [x10, :lo12:g_max_erase_count]
	adrp	x10, g_in_swl_replace
	str	wzr, [x9, :lo12:g_gc_skip_write_count]
	adrp	x9, g_gc_bad_block_temp_num
	str	wzr, [x10, :lo12:g_in_swl_replace]
	adrp	x10, g_cur_erase_blk
	strh	wzr, [x9, :lo12:g_gc_bad_block_temp_num]
	adrp	x9, g_gc_bad_block_gc_index
	lsl	w0, w8, #1
	stp	x28, x27, [sp, #16]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #32]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #48]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #64]             // 16-byte Folded Spill
	mov	x29, sp
	str	wzr, [x10, :lo12:g_cur_erase_blk]
	strh	wzr, [x9, :lo12:g_gc_bad_block_gc_index]
	bl	ftl_malloc
	ldrh	w8, [x19, :lo12:c_ftl_nand_page_pre_super_blk]
	adrp	x9, p_gc_blk_tbl
	str	x0, [x9, :lo12:p_gc_blk_tbl]
	add	w8, w8, w8, lsl #1
	lsl	w0, w8, #2
	bl	ftl_malloc
	adrp	x21, c_ftl_nand_planes_num
	ldrh	w8, [x21, :lo12:c_ftl_nand_planes_num]
	adrp	x9, p_gc_page_info
	str	x0, [x9, :lo12:p_gc_page_info]
	lsl	w20, w8, #7
	mov	w0, w20
	lsl	w19, w8, #5
	bl	ftl_malloc
	adrp	x8, req_read
	str	x0, [x8, :lo12:req_read]
	mov	w0, w19
	bl	ftl_malloc
	adrp	x8, req_gc_dst
	str	x0, [x8, :lo12:req_gc_dst]
	mov	w0, w20
	bl	ftl_malloc
	adrp	x8, req_prgm
	str	x0, [x8, :lo12:req_prgm]
	mov	w0, w19
	bl	ftl_malloc
	adrp	x8, req_erase
	str	x0, [x8, :lo12:req_erase]
	mov	w0, w19
	bl	ftl_malloc
	adrp	x20, c_ftl_nand_byte_pre_page
	ldrh	w8, [x21, :lo12:c_ftl_nand_planes_num]
	ldrh	w19, [x20, :lo12:c_ftl_nand_byte_pre_page]
	adrp	x10, req_gc
	mov	w9, #1
	str	x0, [x10, :lo12:req_gc]
	bfi	w9, w8, #1, #16
	adrp	x22, c_gc_page_buf_num
	mov	w0, w19
	mov	w25, #1
	str	w9, [x22, :lo12:c_gc_page_buf_num]
	bl	ftl_malloc
	adrp	x8, p_sys_data_buf
	str	x0, [x8, :lo12:p_sys_data_buf]
	mov	w0, w19
	bl	ftl_malloc
	adrp	x8, p_sys_data_buf_1
	str	x0, [x8, :lo12:p_sys_data_buf_1]
	mov	w0, w19
	bl	ftl_malloc
	ldr	w8, [x22, :lo12:c_gc_page_buf_num]
	adrp	x9, p_vendor_data_buf
	str	x0, [x9, :lo12:p_vendor_data_buf]
	mul	w0, w8, w19
	bl	ftl_malloc
	adrp	x8, p_gc_data_buf
	str	x0, [x8, :lo12:p_gc_data_buf]
	mov	w0, w19
	bl	ftl_malloc
	adrp	x8, p_io_data_buf_0
	str	x0, [x8, :lo12:p_io_data_buf_0]
	mov	w0, w19
	bl	ftl_malloc
	ldr	w8, [x22, :lo12:c_gc_page_buf_num]
	adrp	x9, p_io_data_buf_1
	str	x0, [x9, :lo12:p_io_data_buf_1]
	add	w8, w8, w8, lsl #1
	lsl	w0, w8, #3
	bl	ftl_malloc
	adrp	x8, gp_gc_page_buf_info
	str	x0, [x8, :lo12:gp_gc_page_buf_info]
	mov	w0, w19
	bl	ftl_malloc
	adrp	x8, sftl_nand_check_buf
	str	x0, [x8, :lo12:sftl_nand_check_buf]
	mov	w0, w19
	bl	ftl_malloc
	adrp	x8, c_ftl_nand_sec_pre_page
	ldrh	w8, [x8, :lo12:c_ftl_nand_sec_pre_page]
	adrp	x9, sftl_temp_buf
	str	x0, [x9, :lo12:sftl_temp_buf]
	lsl	w0, w8, #2
	bl	ftl_malloc
	adrp	x23, c_ftl_nand_byte_pre_oob
	ldrh	w8, [x23, :lo12:c_ftl_nand_byte_pre_oob]
	ldrh	w9, [x21, :lo12:c_ftl_nand_planes_num]
	adrp	x10, sftl_nand_check_spare_buf
	str	x0, [x10, :lo12:sftl_nand_check_spare_buf]
	mul	w19, w9, w8
	mov	w0, w19
	bl	ftl_malloc
	adrp	x8, p_sys_spare_buf
	str	x0, [x8, :lo12:p_sys_spare_buf]
	lsl	w0, w19, #2
	bl	ftl_malloc
	ldrh	w8, [x23, :lo12:c_ftl_nand_byte_pre_oob]
	ldr	w9, [x22, :lo12:c_gc_page_buf_num]
	adrp	x10, p_io_spare_buf
	str	x0, [x10, :lo12:p_io_spare_buf]
	mul	w0, w9, w8
	bl	ftl_malloc
	adrp	x23, c_ftl_nand_blk_pre_plane
	ldrh	w8, [x23, :lo12:c_ftl_nand_blk_pre_plane]
	adrp	x9, p_gc_spare_buf
	adrp	x19, g_ect_tbl_info_size
	str	x0, [x9, :lo12:p_gc_spare_buf]
	lsl	w8, w8, #1
	and	w0, w8, #0xfffe
	strh	w8, [x19, :lo12:g_ect_tbl_info_size]
	bl	ftl_malloc
	ldrh	w8, [x19, :lo12:g_ect_tbl_info_size]
	adrp	x9, p_swl_mul_table
	str	x0, [x9, :lo12:p_swl_mul_table]
	add	w8, w8, #547                    // =547
	lsr	w9, w8, #9
	and	w0, w8, #0x1fe00
	strh	w9, [x19, :lo12:g_ect_tbl_info_size]
	bl	ftl_malloc
	ldrh	w9, [x23, :lo12:c_ftl_nand_blk_pre_plane]
	adrp	x8, gp_ect_tbl_info
	str	x0, [x8, :lo12:gp_ect_tbl_info]
	add	x8, x0, #32                     // =32
	lsl	w19, w9, #1
	adrp	x10, p_erase_count_table
	mov	w0, w19
	str	x8, [x10, :lo12:p_erase_count_table]
	bl	ftl_malloc
	adrp	x8, p_valid_page_count_check_table
	str	x0, [x8, :lo12:p_valid_page_count_check_table]
	mov	w0, w19
	bl	ftl_malloc
	adrp	x21, c_ftl_nand_max_map_blks
	ldr	w8, [x21, :lo12:c_ftl_nand_max_map_blks]
	adrp	x9, p_valid_page_count_table
	str	x0, [x9, :lo12:p_valid_page_count_table]
	lsl	w19, w8, #1
	mov	w0, w19
	bl	ftl_malloc
	adrp	x26, p_map_block_table
	str	x0, [x26, :lo12:p_map_block_table]
	mov	w0, w19
	bl	ftl_malloc
	adrp	x19, c_ftl_nand_max_vendor_blks
	ldrh	w8, [x19, :lo12:c_ftl_nand_max_vendor_blks]
	adrp	x22, p_map_block_valid_page_count
	str	x0, [x22, :lo12:p_map_block_valid_page_count]
	lsl	w0, w8, #1
	bl	ftl_malloc
	ldrh	w8, [x19, :lo12:c_ftl_nand_max_vendor_blks]
	adrp	x9, p_vendor_block_table
	str	x0, [x9, :lo12:p_vendor_block_table]
	lsl	w0, w8, #1
	bl	ftl_malloc
	ldrh	w8, [x19, :lo12:c_ftl_nand_max_vendor_blks]
	adrp	x9, p_vendor_block_valid_page_count
	str	x0, [x9, :lo12:p_vendor_block_valid_page_count]
	lsl	w0, w8, #2
	bl	ftl_malloc
	adrp	x19, c_ftl_nand_vendor_region_num
	ldrh	w8, [x19, :lo12:c_ftl_nand_vendor_region_num]
	adrp	x9, p_vendor_block_ver_table
	str	x0, [x9, :lo12:p_vendor_block_ver_table]
	lsl	w0, w8, #2
	bl	ftl_malloc
	ldrh	w8, [x19, :lo12:c_ftl_nand_vendor_region_num]
	adrp	x9, p_vendor_region_ppn_table
	mov	w1, wzr
	str	x0, [x9, :lo12:p_vendor_region_ppn_table]
	lsl	x2, x8, #2
	bl	memset
	adrp	x8, c_ftl_nand_map_region_num
	ldrh	w8, [x8, :lo12:c_ftl_nand_map_region_num]
	lsl	w0, w8, #2
	bl	ftl_malloc
	ldr	w8, [x21, :lo12:c_ftl_nand_max_map_blks]
	adrp	x21, p_map_region_ppn_table
	str	x0, [x21, :lo12:p_map_region_ppn_table]
	lsl	w0, w8, #2
	bl	ftl_malloc
	adrp	x24, c_ftl_nand_l2pmap_ram_region_num
	ldrh	w8, [x24, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	adrp	x19, p_map_block_ver_table
	str	x0, [x19, :lo12:p_map_block_ver_table]
	lsl	w0, w8, #4
	bl	ftl_malloc
	ldrh	w8, [x24, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	ldrh	w9, [x20, :lo12:c_ftl_nand_byte_pre_page]
	adrp	x20, p_l2p_ram_map
	str	x0, [x20, :lo12:p_l2p_ram_map]
	mul	w0, w9, w8
	bl	ftl_malloc
	ldrh	w8, [x23, :lo12:c_ftl_nand_blk_pre_plane]
	adrp	x24, p_l2p_map_buf
	str	x0, [x24, :lo12:p_l2p_map_buf]
	add	w8, w8, w8, lsl #1
	lsl	w0, w8, #1
	bl	ftl_malloc
	adrp	x8, c_ftl_nand_blks_per_die
	ldrh	w8, [x8, :lo12:c_ftl_nand_blks_per_die]
	adrp	x27, c_ftl_nand_die_num
	ldrh	w9, [x27, :lo12:c_ftl_nand_die_num]
	adrp	x28, c_ftl_nand_bbm_buf_size
	add	w8, w8, #31                     // =31
	lsr	w8, w8, #5
	adrp	x23, p_data_block_list_table
	strh	w8, [x28, :lo12:c_ftl_nand_bbm_buf_size]
	mul	w8, w9, w8
	str	x0, [x23, :lo12:p_data_block_list_table]
	lsl	w0, w8, #2
	bl	ftl_malloc
	ldrh	w9, [x27, :lo12:c_ftl_nand_die_num]
	adrp	x8, gBbtInfo
	adrp	x27, gBbtInfo+32
	add	x8, x8, :lo12:gBbtInfo
	cmp	w9, #2                          // =2
	str	x0, [x27, :lo12:gBbtInfo+32]
	b.lo	.LBB10_9
// %bb.1:
	ldrh	w10, [x28, :lo12:c_ftl_nand_bbm_buf_size]
	cmp	w9, #2                          // =2
	mov	w11, #2
	csel	w25, w9, w11, hi
	sub	x12, x25, #1                    // =1
	cmp	x12, #2                         // =2
	lsl	x9, x10, #2
	b.hs	.LBB10_3
// %bb.2:
	mov	w11, #1
	b	.LBB10_6
.LBB10_3:
	and	x13, x12, #0xfffffffffffffffe
	adrp	x15, gBbtInfo+48
	orr	x11, x12, #0x1
	lsl	x14, x10, #3
	add	x15, x15, :lo12:gBbtInfo+48
	mov	x16, x13
	mov	x17, x0
.LBB10_4:                               // =>This Inner Loop Header: Depth=1
	add	x18, x17, x9
	add	x17, x17, x14
	subs	x16, x16, #2                    // =2
	stp	x18, x17, [x15, #-8]
	add	x15, x15, #16                   // =16
	b.ne	.LBB10_4
// %bb.5:
	cmp	x12, x13
	b.eq	.LBB10_8
.LBB10_6:
	add	x12, x8, x11, lsl #3
	mul	x13, x11, x10
	add	x10, x12, #32                   // =32
	add	x12, x0, x13, lsl #2
	sub	x11, x25, x11
.LBB10_7:                               // =>This Inner Loop Header: Depth=1
	str	x12, [x10], #8
	subs	x11, x11, #1                    // =1
	add	x12, x12, x9
	b.ne	.LBB10_7
.LBB10_8:
	cmp	w25, #7                         // =7
	b.hi	.LBB10_10
.LBB10_9:
	add	x8, x8, w25, uxtw #3
	mov	w9, #7
	add	x0, x8, #32                     // =32
	sub	w8, w9, w25
	lsl	x8, x8, #3
	add	x2, x8, #8                      // =8
	mov	w1, wzr
	bl	memset
.LBB10_10:
	ldr	x8, [x26, :lo12:p_map_block_table]
	cbz	x8, .LBB10_43
// %bb.11:
	ldr	x8, [x22, :lo12:p_map_block_valid_page_count]
	cbz	x8, .LBB10_43
// %bb.12:
	ldr	x8, [x21, :lo12:p_map_region_ppn_table]
	cbz	x8, .LBB10_43
// %bb.13:
	ldr	x8, [x19, :lo12:p_map_block_ver_table]
	cbz	x8, .LBB10_43
// %bb.14:
	ldr	x8, [x20, :lo12:p_l2p_ram_map]
	cbz	x8, .LBB10_43
// %bb.15:
	ldr	x8, [x24, :lo12:p_l2p_map_buf]
	cbz	x8, .LBB10_43
// %bb.16:
	ldr	x8, [x23, :lo12:p_data_block_list_table]
	cbz	x8, .LBB10_43
// %bb.17:
	ldr	x8, [x27, :lo12:gBbtInfo+32]
	cbz	x8, .LBB10_43
// %bb.18:
	adrp	x8, p_valid_page_count_table
	ldr	x8, [x8, :lo12:p_valid_page_count_table]
	cbz	x8, .LBB10_43
// %bb.19:
	adrp	x8, p_gc_blk_tbl
	ldr	x8, [x8, :lo12:p_gc_blk_tbl]
	cbz	x8, .LBB10_43
// %bb.20:
	adrp	x8, p_gc_page_info
	ldr	x8, [x8, :lo12:p_gc_page_info]
	cbz	x8, .LBB10_43
// %bb.21:
	adrp	x8, req_read
	ldr	x8, [x8, :lo12:req_read]
	cbz	x8, .LBB10_43
// %bb.22:
	adrp	x8, req_prgm
	ldr	x8, [x8, :lo12:req_prgm]
	cbz	x8, .LBB10_43
// %bb.23:
	adrp	x8, req_erase
	ldr	x8, [x8, :lo12:req_erase]
	cbz	x8, .LBB10_43
// %bb.24:
	adrp	x8, req_gc
	ldr	x8, [x8, :lo12:req_gc]
	cbz	x8, .LBB10_43
// %bb.25:
	adrp	x8, req_gc_dst
	ldr	x8, [x8, :lo12:req_gc_dst]
	cbz	x8, .LBB10_43
// %bb.26:
	adrp	x8, p_sys_data_buf
	ldr	x8, [x8, :lo12:p_sys_data_buf]
	cbz	x8, .LBB10_43
// %bb.27:
	adrp	x8, p_sys_data_buf_1
	ldr	x8, [x8, :lo12:p_sys_data_buf_1]
	cbz	x8, .LBB10_43
// %bb.28:
	adrp	x8, p_vendor_data_buf
	ldr	x8, [x8, :lo12:p_vendor_data_buf]
	cbz	x8, .LBB10_43
// %bb.29:
	adrp	x8, p_gc_data_buf
	ldr	x8, [x8, :lo12:p_gc_data_buf]
	cbz	x8, .LBB10_43
// %bb.30:
	adrp	x8, p_io_data_buf_0
	ldr	x8, [x8, :lo12:p_io_data_buf_0]
	cbz	x8, .LBB10_43
// %bb.31:
	adrp	x8, p_io_data_buf_1
	ldr	x8, [x8, :lo12:p_io_data_buf_1]
	cbz	x8, .LBB10_43
// %bb.32:
	adrp	x8, gp_gc_page_buf_info
	ldr	x8, [x8, :lo12:gp_gc_page_buf_info]
	cbz	x8, .LBB10_43
// %bb.33:
	adrp	x8, p_sys_spare_buf
	ldr	x8, [x8, :lo12:p_sys_spare_buf]
	cbz	x8, .LBB10_43
// %bb.34:
	adrp	x8, p_io_spare_buf
	ldr	x8, [x8, :lo12:p_io_spare_buf]
	cbz	x8, .LBB10_43
// %bb.35:
	adrp	x8, p_gc_spare_buf
	ldr	x8, [x8, :lo12:p_gc_spare_buf]
	cbz	x8, .LBB10_43
// %bb.36:
	adrp	x8, p_erase_count_table
	ldr	x8, [x8, :lo12:p_erase_count_table]
	cbz	x8, .LBB10_43
// %bb.37:
	adrp	x8, p_swl_mul_table
	ldr	x8, [x8, :lo12:p_swl_mul_table]
	cbz	x8, .LBB10_43
// %bb.38:
	adrp	x8, p_vendor_block_table
	ldr	x8, [x8, :lo12:p_vendor_block_table]
	cbz	x8, .LBB10_43
// %bb.39:
	adrp	x8, p_vendor_block_valid_page_count
	ldr	x8, [x8, :lo12:p_vendor_block_valid_page_count]
	cbz	x8, .LBB10_43
// %bb.40:
	adrp	x8, p_vendor_block_ver_table
	ldr	x8, [x8, :lo12:p_vendor_block_ver_table]
	cbz	x8, .LBB10_43
// %bb.41:
	adrp	x8, p_vendor_region_ppn_table
	ldr	x8, [x8, :lo12:p_vendor_region_ppn_table]
	cbz	x8, .LBB10_43
// %bb.42:
	mov	w0, wzr
	b	.LBB10_44
.LBB10_43:
	adrp	x0, .L.str.76
	adrp	x1, .L__func__.FtlMemInit
	add	x0, x0, :lo12:.L.str.76
	add	x1, x1, :lo12:.L__func__.FtlMemInit
	bl	sftl_printk
	mov	w0, #-1
.LBB10_44:
	ldp	x20, x19, [sp, #80]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #64]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #48]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #32]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #16]             // 16-byte Folded Reload
	ldp	x29, x30, [sp], #96             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end10:
	.size	FtlMemInit, .Lfunc_end10-FtlMemInit
                                        // -- End function
	.globl	ftl_malloc                      // -- Begin function ftl_malloc
	.p2align	2
	.type	ftl_malloc,@function
ftl_malloc:                             // @ftl_malloc
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
                                        // kill: def $w0 killed $w0 def $x0
	mov	w1, #3265
	sxtw	x0, w0
	mov	x29, sp
	bl	__kmalloc
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end11:
	.size	ftl_malloc, .Lfunc_end11-ftl_malloc
                                        // -- End function
	.globl	ftl_memset                      // -- Begin function ftl_memset
	.p2align	2
	.type	ftl_memset,@function
ftl_memset:                             // @ftl_memset
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	mov	w2, w2
	str	x19, [sp, #16]                  // 8-byte Folded Spill
	mov	x29, sp
	mov	x19, x0
	bl	memset
	mov	x0, x19
	ldr	x19, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end12:
	.size	ftl_memset, .Lfunc_end12-ftl_memset
                                        // -- End function
	.globl	FtlVariablesInit                // -- Begin function FtlVariablesInit
	.p2align	2
	.type	FtlVariablesInit,@function
FtlVariablesInit:                       // @FtlVariablesInit
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-64]!           // 16-byte Folded Spill
	adrp	x9, g_recovery_page_min_ver
	mov	w10, #-1
	str	w10, [x9, :lo12:g_recovery_page_min_ver]
	adrp	x9, g_totle_swl_count
	stp	x20, x19, [sp, #48]             // 16-byte Folded Spill
	adrp	x8, g_tmp_data_superblock_id
	mov	w19, #65535
	str	wzr, [x9, :lo12:g_totle_swl_count]
	adrp	x9, c_ftl_nand_max_vendor_blks
	strh	w19, [x8, :lo12:g_tmp_data_superblock_id]
	adrp	x8, p_vendor_block_table
	ldrh	w9, [x9, :lo12:c_ftl_nand_max_vendor_blks]
	ldr	x0, [x8, :lo12:p_vendor_block_table]
	adrp	x10, ftl_gc_temp_power_lost_recovery_flag
	adrp	x8, g_totle_vendor_block
	lsl	x2, x9, #1
	mov	w1, wzr
	stp	x24, x23, [sp, #16]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #32]             // 16-byte Folded Spill
	mov	x29, sp
	str	wzr, [x10, :lo12:ftl_gc_temp_power_lost_recovery_flag]
	strh	wzr, [x8, :lo12:g_totle_vendor_block]
	bl	memset
	adrp	x20, c_ftl_nand_blk_pre_plane
	adrp	x8, p_erase_count_table
	ldrh	w9, [x20, :lo12:c_ftl_nand_blk_pre_plane]
	ldr	x0, [x8, :lo12:p_erase_count_table]
	mov	w1, wzr
	lsl	x2, x9, #1
	bl	memset
	adrp	x8, p_swl_mul_table
	ldrh	w9, [x20, :lo12:c_ftl_nand_blk_pre_plane]
	ldr	x0, [x8, :lo12:p_swl_mul_table]
	mov	w1, wzr
	lsl	x2, x9, #1
	bl	memset
	adrp	x8, g_sys_save_data
	adrp	x0, g_sys_ext_data
	add	x8, x8, :lo12:g_sys_save_data
	add	x0, x0, :lo12:g_sys_ext_data
	mov	w2, #512
	mov	w1, wzr
	stp	xzr, xzr, [x8]
	stp	xzr, xzr, [x8, #16]
	stp	xzr, xzr, [x8, #32]
	bl	memset
	bl	FtlGcBufInit
	adrp	x21, c_ftl_nand_max_map_blks
	adrp	x20, p_map_block_valid_page_count
	ldr	w8, [x21, :lo12:c_ftl_nand_max_map_blks]
	ldr	x0, [x20, :lo12:p_map_block_valid_page_count]
	mov	w1, wzr
	lsl	w2, w8, #1
	bl	memset
	adrp	x23, c_ftl_nand_byte_pre_page
	adrp	x24, c_ftl_nand_l2pmap_ram_region_num
	adrp	x22, p_l2p_map_buf
	ldrh	w8, [x23, :lo12:c_ftl_nand_byte_pre_page]
	ldrh	w9, [x24, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	ldr	x0, [x22, :lo12:p_l2p_map_buf]
	mov	w1, #255
	mul	x2, x9, x8
	bl	memset
	ldrh	w8, [x24, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	cbz	w8, .LBB13_3
// %bb.1:
	mov	x8, xzr
	mov	x9, xzr
	mov	x10, xzr
	adrp	x11, p_l2p_ram_map
	mov	x12, #4294967296
.LBB13_2:                               // =>This Inner Loop Header: Depth=1
	ldr	x13, [x11, :lo12:p_l2p_ram_map]
	add	x10, x10, #1                    // =1
	add	x13, x13, x8
	str	wzr, [x13, #4]
	ldr	x13, [x11, :lo12:p_l2p_ram_map]
	strh	w19, [x13, x8]
	ldrh	w13, [x23, :lo12:c_ftl_nand_byte_pre_page]
	ldr	x14, [x22, :lo12:p_l2p_map_buf]
	ldr	x15, [x11, :lo12:p_l2p_ram_map]
	mul	x13, x9, x13
	asr	x13, x13, #32
	and	x13, x13, #0xfffffffffffffffc
	add	x13, x14, x13
	add	x14, x15, x8
	str	x13, [x14, #8]
	ldrh	w13, [x24, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	add	x9, x9, x12
	add	x8, x8, #16                     // =16
	cmp	x10, x13
	b.lo	.LBB13_2
.LBB13_3:
	adrp	x8, gL2pMapInfo
	add	x8, x8, :lo12:gL2pMapInfo
	mov	w9, #-255655937
	stur	w9, [x8, #2]
	adrp	x9, g_totle_map_block
	adrp	x11, c_ftl_nand_map_region_num
	ldr	w10, [x21, :lo12:c_ftl_nand_max_map_blks]
	ldrh	w9, [x9, :lo12:g_totle_map_block]
	ldrh	w11, [x11, :lo12:c_ftl_nand_map_region_num]
	ldr	x12, [x20, :lo12:p_map_block_valid_page_count]
	strh	w10, [x8, #10]
	adrp	x10, p_map_block_table
	strh	w9, [x8, #8]
	adrp	x9, p_map_block_ver_table
	strh	w11, [x8, #6]
	adrp	x11, p_map_region_ppn_table
	ldr	x10, [x10, :lo12:p_map_block_table]
	ldr	x9, [x9, :lo12:p_map_block_ver_table]
	ldr	x11, [x11, :lo12:p_map_region_ppn_table]
	strh	w19, [x8]
	ldp	x20, x19, [sp, #48]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #32]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #16]             // 16-byte Folded Reload
	stp	x10, x9, [x8, #16]
	stp	x12, x11, [x8, #32]
	mov	w0, wzr
	ldp	x29, x30, [sp], #64             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end13:
	.size	FtlVariablesInit, .Lfunc_end13-FtlVariablesInit
                                        // -- End function
	.globl	FtlGcBufInit                    // -- Begin function FtlGcBufInit
	.p2align	2
	.type	FtlGcBufInit,@function
FtlGcBufInit:                           // @FtlGcBufInit
// %bb.0:
	hint	#25
	adrp	x8, c_ftl_nand_planes_num
	ldrh	w9, [x8, :lo12:c_ftl_nand_planes_num]
	adrp	x10, g_gc_num_req
	str	wzr, [x10, :lo12:g_gc_num_req]
	cbz	w9, .LBB14_4
// %bb.1:
	mov	x9, xzr
	mov	x10, xzr
	mov	x11, xzr
	adrp	x12, gp_gc_page_buf_info
	mov	w13, #1
	adrp	x14, p_gc_data_buf
	adrp	x15, c_ftl_nand_byte_pre_page
	adrp	x16, p_gc_spare_buf
	adrp	x17, c_ftl_nand_byte_pre_oob
	adrp	x18, req_gc
.LBB14_2:                               // =>This Inner Loop Header: Depth=1
	ldr	x0, [x12, :lo12:gp_gc_page_buf_info]
	add	x0, x0, x10
	str	w13, [x0, #16]
	ldrh	w0, [x15, :lo12:c_ftl_nand_byte_pre_page]
	ldr	x1, [x14, :lo12:p_gc_data_buf]
	ldr	x2, [x12, :lo12:gp_gc_page_buf_info]
	mul	w0, w11, w0
	add	w3, w0, #3                      // =3
	cmp	w0, #0                          // =0
	csel	w0, w3, w0, lt
	asr	w0, w0, #2
	add	x0, x1, w0, sxtw #2
	str	x0, [x2, x10]
	ldrh	w0, [x17, :lo12:c_ftl_nand_byte_pre_oob]
	ldr	x1, [x16, :lo12:p_gc_spare_buf]
	ldr	x2, [x12, :lo12:gp_gc_page_buf_info]
	mul	w0, w11, w0
	add	w3, w0, #3                      // =3
	cmp	w0, #0                          // =0
	csel	w0, w3, w0, lt
	asr	w0, w0, #2
	add	x2, x2, x10
	add	x0, x1, w0, sxtw #2
	str	x0, [x2, #8]
	ldr	x0, [x12, :lo12:gp_gc_page_buf_info]
	ldr	x1, [x18, :lo12:req_gc]
	add	x11, x11, #1                    // =1
	ldr	x0, [x0, x10]
	add	x1, x1, x9
	str	x0, [x1, #8]
	ldr	x0, [x12, :lo12:gp_gc_page_buf_info]
	ldr	x1, [x18, :lo12:req_gc]
	add	x0, x0, x10
	ldr	x0, [x0, #8]
	add	x1, x1, x9
	add	x10, x10, #24                   // =24
	add	x9, x9, #32                     // =32
	str	x0, [x1, #16]
	ldrh	w0, [x8, :lo12:c_ftl_nand_planes_num]
	cmp	x11, x0
	b.lo	.LBB14_2
// %bb.3:
	mov	w8, w0
	b	.LBB14_5
.LBB14_4:
	mov	w8, wzr
.LBB14_5:
	adrp	x9, c_gc_page_buf_num
	ldr	w10, [x9, :lo12:c_gc_page_buf_num]
	cmp	w10, w8
	b.ls	.LBB14_8
// %bb.6:
	adrp	x10, gp_gc_page_buf_info
	adrp	x11, p_gc_data_buf
	adrp	x12, c_ftl_nand_byte_pre_page
	adrp	x13, p_gc_spare_buf
	adrp	x14, c_ftl_nand_byte_pre_oob
.LBB14_7:                               // =>This Inner Loop Header: Depth=1
	ldr	x15, [x10, :lo12:gp_gc_page_buf_info]
	and	x16, x8, #0xffff
	add	x16, x16, w8, uxth #1
	lsl	x16, x16, #3
	add	x15, x15, x16
	str	wzr, [x15, #16]
	ldrh	w15, [x12, :lo12:c_ftl_nand_byte_pre_page]
	ldr	x17, [x11, :lo12:p_gc_data_buf]
	ldr	x18, [x10, :lo12:gp_gc_page_buf_info]
	mul	w15, w8, w15
	add	w0, w15, #3                     // =3
	cmp	w15, #0                         // =0
	csel	w15, w0, w15, lt
	asr	w15, w15, #2
	add	x15, x17, w15, sxtw #2
	str	x15, [x18, x16]
	ldrh	w15, [x14, :lo12:c_ftl_nand_byte_pre_oob]
	ldr	x17, [x13, :lo12:p_gc_spare_buf]
	ldr	x18, [x10, :lo12:gp_gc_page_buf_info]
	mul	w15, w8, w15
	add	w0, w15, #3                     // =3
	cmp	w15, #0                         // =0
	csel	w15, w0, w15, lt
	asr	w15, w15, #2
	add	x15, x17, w15, sxtw #2
	add	x16, x18, x16
	str	x15, [x16, #8]
	ldr	w15, [x9, :lo12:c_gc_page_buf_num]
	add	w8, w8, #1                      // =1
	and	w8, w8, #0xffff
	cmp	w15, w8
	b.hi	.LBB14_7
.LBB14_8:
	hint	#29
	ret
.Lfunc_end14:
	.size	FtlGcBufInit, .Lfunc_end14-FtlGcBufInit
                                        // -- End function
	.globl	FtlL2PDataInit                  // -- Begin function FtlL2PDataInit
	.p2align	2
	.type	FtlL2PDataInit,@function
FtlL2PDataInit:                         // @FtlL2PDataInit
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-64]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #48]             // 16-byte Folded Spill
	adrp	x20, c_ftl_nand_max_map_blks
	adrp	x19, p_map_block_valid_page_count
	ldr	w8, [x20, :lo12:c_ftl_nand_max_map_blks]
	ldr	x0, [x19, :lo12:p_map_block_valid_page_count]
	mov	w1, wzr
	str	x23, [sp, #16]                  // 8-byte Folded Spill
	lsl	w2, w8, #1
	stp	x22, x21, [sp, #32]             // 16-byte Folded Spill
	mov	x29, sp
	bl	memset
	adrp	x22, c_ftl_nand_byte_pre_page
	adrp	x23, c_ftl_nand_l2pmap_ram_region_num
	adrp	x21, p_l2p_map_buf
	ldrh	w8, [x22, :lo12:c_ftl_nand_byte_pre_page]
	ldrh	w9, [x23, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	ldr	x0, [x21, :lo12:p_l2p_map_buf]
	mov	w1, #255
	mul	x2, x9, x8
	bl	memset
	ldrh	w8, [x23, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	cbz	w8, .LBB15_3
// %bb.1:
	mov	x8, xzr
	mov	x9, xzr
	mov	x10, xzr
	adrp	x11, p_l2p_ram_map
	mov	w12, #65535
	mov	x13, #4294967296
.LBB15_2:                               // =>This Inner Loop Header: Depth=1
	ldr	x14, [x11, :lo12:p_l2p_ram_map]
	add	x10, x10, #1                    // =1
	add	x14, x14, x9
	str	wzr, [x14, #4]
	ldr	x14, [x11, :lo12:p_l2p_ram_map]
	strh	w12, [x14, x9]
	ldrh	w14, [x22, :lo12:c_ftl_nand_byte_pre_page]
	ldr	x15, [x21, :lo12:p_l2p_map_buf]
	ldr	x16, [x11, :lo12:p_l2p_ram_map]
	mul	x14, x8, x14
	asr	x14, x14, #32
	and	x14, x14, #0xfffffffffffffffc
	add	x14, x15, x14
	add	x15, x16, x9
	str	x14, [x15, #8]
	ldrh	w14, [x23, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	add	x9, x9, #16                     // =16
	add	x8, x8, x13
	cmp	x10, x14
	b.lo	.LBB15_2
.LBB15_3:
	adrp	x8, gL2pMapInfo
	add	x8, x8, :lo12:gL2pMapInfo
	mov	w9, #-255655937
	mov	w10, #65535
	stur	w9, [x8, #2]
	adrp	x9, g_totle_map_block
	strh	w10, [x8]
	adrp	x10, c_ftl_nand_map_region_num
	ldr	w11, [x20, :lo12:c_ftl_nand_max_map_blks]
	ldrh	w9, [x9, :lo12:g_totle_map_block]
	ldrh	w10, [x10, :lo12:c_ftl_nand_map_region_num]
	ldr	x12, [x19, :lo12:p_map_block_valid_page_count]
	strh	w11, [x8, #10]
	adrp	x11, p_map_block_table
	strh	w9, [x8, #8]
	adrp	x9, p_map_block_ver_table
	strh	w10, [x8, #6]
	adrp	x10, p_map_region_ppn_table
	ldr	x11, [x11, :lo12:p_map_block_table]
	ldr	x9, [x9, :lo12:p_map_block_ver_table]
	ldr	x10, [x10, :lo12:p_map_region_ppn_table]
	ldp	x20, x19, [sp, #48]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #32]             // 16-byte Folded Reload
	ldr	x23, [sp, #16]                  // 8-byte Folded Reload
	stp	x11, x9, [x8, #16]
	stp	x12, x10, [x8, #32]
	ldp	x29, x30, [sp], #64             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end15:
	.size	FtlL2PDataInit, .Lfunc_end15-FtlL2PDataInit
                                        // -- End function
	.globl	IsBlkInVendorPart               // -- Begin function IsBlkInVendorPart
	.p2align	2
	.type	IsBlkInVendorPart,@function
IsBlkInVendorPart:                      // @IsBlkInVendorPart
// %bb.0:
	hint	#25
	adrp	x8, g_totle_vendor_block
	ldrh	w9, [x8, :lo12:g_totle_vendor_block]
	mov	w8, wzr
	cbz	w9, .LBB16_7
// %bb.1:
	adrp	x9, c_ftl_nand_max_vendor_blks
	ldrh	w9, [x9, :lo12:c_ftl_nand_max_vendor_blks]
	cbz	w9, .LBB16_7
// %bb.2:
	adrp	x8, p_vendor_block_table
	ldr	x8, [x8, :lo12:p_vendor_block_table]
	cmp	w9, #1                          // =1
	csinc	w9, w9, wzr, hi
.LBB16_3:                               // =>This Inner Loop Header: Depth=1
	ldrh	w10, [x8]
	cmp	w10, w0, uxth
	b.eq	.LBB16_6
// %bb.4:                               //   in Loop: Header=BB16_3 Depth=1
	subs	x9, x9, #1                      // =1
	add	x8, x8, #2                      // =2
	b.ne	.LBB16_3
// %bb.5:
	mov	w8, wzr
	b	.LBB16_7
.LBB16_6:
	mov	w8, #1
.LBB16_7:
	mov	w0, w8
	hint	#29
	ret
.Lfunc_end16:
	.size	IsBlkInVendorPart, .Lfunc_end16-IsBlkInVendorPart
                                        // -- End function
	.globl	FtlLowFormatEraseBlock          // -- Begin function FtlLowFormatEraseBlock
	.p2align	2
	.type	FtlLowFormatEraseBlock,@function
FtlLowFormatEraseBlock:                 // @FtlLowFormatEraseBlock
// %bb.0:
	hint	#25
	sub	sp, sp, #112                    // =112
	adrp	x17, c_ftl_nand_planes_num
	ldrh	w8, [x17, :lo12:c_ftl_nand_planes_num]
	and	w9, w0, #0xffff
	adrp	x10, g_cur_erase_blk
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	stp	x28, x27, [sp, #32]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #48]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #64]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #80]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #96]             // 16-byte Folded Spill
	add	x29, sp, #16                    // =16
	str	w9, [x10, :lo12:g_cur_erase_blk]
	cbz	w8, .LBB17_35
// %bb.1:
	adrp	x18, p_plane_order_table
	adrp	x23, gBbtInfo
	mov	w19, w1
	mov	w20, w0
	mov	x8, xzr
	mov	w21, wzr
	mov	w24, wzr
	adrp	x25, req_erase
	add	x18, x18, :lo12:p_plane_order_table
	adrp	x16, c_ftl_nand_planes_per_die
	adrp	x27, c_ftl_nand_blks_per_die
	add	x23, x23, :lo12:gBbtInfo
	adrp	x0, p_io_data_buf_1
	adrp	x9, p_io_spare_buf
	adrp	x1, c_ftl_nand_byte_pre_oob
	adrp	x2, g_totle_vendor_block
	adrp	x3, c_ftl_nand_max_vendor_blks
	adrp	x4, p_vendor_block_table
	b	.LBB17_5
.LBB17_2:                               //   in Loop: Header=BB17_5 Depth=1
	and	w12, w11, #0xffff
	and	w13, w10, #0xffff
	udiv	w13, w12, w13
	add	x14, x23, w13, uxtw #3
	ldr	x14, [x14, #32]
	msub	w10, w13, w10, w11
	lsr	w11, w10, #3
	and	x11, x11, #0x1ffc
	ldr	w11, [x14, x11]
	lsr	w10, w11, w10
	tbz	w10, #0, .LBB17_11
// %bb.3:                               //   in Loop: Header=BB17_5 Depth=1
	add	w24, w24, #1                    // =1
.LBB17_4:                               //   in Loop: Header=BB17_5 Depth=1
	ldrh	w10, [x17, :lo12:c_ftl_nand_planes_num]
	add	x8, x8, #1                      // =1
	cmp	x8, x10
	b.hs	.LBB17_12
.LBB17_5:                               // =>This Loop Header: Depth=1
                                        //     Child Loop BB17_9 Depth 2
	ldr	x10, [x25, :lo12:req_erase]
	lsl	x11, x8, #5
	tst	w19, #0xff
	str	wzr, [x10, x11]
	ldrb	w11, [x18, x8]
	ldrh	w12, [x16, :lo12:c_ftl_nand_planes_per_die]
	ldrh	w10, [x27, :lo12:c_ftl_nand_blks_per_die]
	and	w13, w11, #0xffff
	and	w14, w12, #0xffff
	mul	w15, w12, w20
	udiv	w13, w13, w14
	madd	w14, w10, w13, w15
	msub	w11, w13, w12, w11
	add	w11, w14, w11
	b.eq	.LBB17_2
// %bb.6:                               //   in Loop: Header=BB17_5 Depth=1
	ldrh	w12, [x2, :lo12:g_totle_vendor_block]
	cbz	w12, .LBB17_2
// %bb.7:                               //   in Loop: Header=BB17_5 Depth=1
	ldrh	w13, [x3, :lo12:c_ftl_nand_max_vendor_blks]
	cbz	w13, .LBB17_2
// %bb.8:                               //   in Loop: Header=BB17_5 Depth=1
	ldr	x12, [x4, :lo12:p_vendor_block_table]
	cmp	w13, #1                         // =1
	csinc	w13, w13, wzr, hi
.LBB17_9:                               //   Parent Loop BB17_5 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldrh	w14, [x12]
	cmp	w14, w11, uxth
	b.eq	.LBB17_4
// %bb.10:                              //   in Loop: Header=BB17_9 Depth=2
	subs	x13, x13, #1                    // =1
	add	x12, x12, #2                    // =2
	b.ne	.LBB17_9
	b	.LBB17_2
.LBB17_11:                              //   in Loop: Header=BB17_5 Depth=1
	ldr	x10, [x25, :lo12:req_erase]
	lsl	w11, w12, #10
	and	x12, x21, #0xffff
	lsl	x12, x12, #5
	add	x10, x10, x12
	str	w11, [x10, #4]
	ldr	x10, [x25, :lo12:req_erase]
	ldr	x11, [x0, :lo12:p_io_data_buf_1]
	and	w13, w21, #0xffff
	add	w21, w21, #1                    // =1
	add	x10, x10, x12
	str	x11, [x10, #8]
	ldrh	w10, [x1, :lo12:c_ftl_nand_byte_pre_oob]
	ldr	x11, [x25, :lo12:req_erase]
	mul	w10, w10, w13
	ldr	x13, [x9, :lo12:p_io_spare_buf]
	add	x11, x11, x12
	add	w12, w10, #3                    // =3
	cmp	w10, #0                         // =0
	csel	w10, w12, w10, lt
	asr	w10, w10, #2
	add	x10, x13, w10, sxtw #2
	str	x10, [x11, #16]
	b	.LBB17_4
.LBB17_12:
	tst	w21, #0xffff
	b.eq	.LBB17_36
// %bb.13:
	ldr	x0, [x25, :lo12:req_erase]
	and	w2, w21, #0xffff
	bl	FlashEraseBlocks
                                        // kill: def $w21 killed $w21 killed $x21 def $x21
	and	x8, x21, #0xffff
	adrp	x22, .L.str.82
	mov	x26, xzr
	mov	w21, #1
	lsl	x28, x8, #5
	add	x22, x22, :lo12:.L.str.82
	b	.LBB17_15
.LBB17_14:                              //   in Loop: Header=BB17_15 Depth=1
	add	x26, x26, #32                   // =32
	cmp	x28, x26
	b.eq	.LBB17_17
.LBB17_15:                              // =>This Inner Loop Header: Depth=1
	ldr	x8, [x25, :lo12:req_erase]
	ldr	w9, [x8, x26]
	cmn	w9, #1                          // =1
	b.ne	.LBB17_14
// %bb.16:                              //   in Loop: Header=BB17_15 Depth=1
	add	x8, x8, x26
	ldr	w8, [x8, #4]
	ldrh	w9, [x27, :lo12:c_ftl_nand_blks_per_die]
	mov	x0, x22
	add	w24, w24, #1                    // =1
	ubfx	w1, w8, #10, #16
	udiv	w2, w1, w9
	lsr	w10, w8, #10
	add	x8, x23, w2, uxtw #3
	ldr	x8, [x8, #32]
	msub	w9, w2, w9, w10
	ubfx	w10, w9, #3, #13
	and	x10, x10, #0x1ffc
	ldr	w11, [x8, x10]
	and	w3, w9, #0xffff
	lsl	w9, w21, w9
	orr	w4, w9, w11
	str	w4, [x8, x10]
	bl	sftl_printk
	ldrh	w8, [x23, #6]
	add	w8, w8, #1                      // =1
	strh	w8, [x23, #6]
	b	.LBB17_14
.LBB17_17:
	adrp	x8, c_ftl_nand_page_pre_slc_blk
	ldrh	w8, [x8, :lo12:c_ftl_nand_page_pre_slc_blk]
	tst	w19, #0xff
	mov	w9, #2
	adrp	x22, .L.str.82
	mov	w5, wzr
	adrp	x6, p_io_data_buf_0
	cset	w10, ne
	csel	w8, w9, w8, eq
	add	x22, x22, :lo12:.L.str.82
	adrp	x16, c_ftl_nand_planes_per_die
	stp	w8, w10, [sp, #4]               // 8-byte Folded Spill
	b	.LBB17_19
.LBB17_18:                              //   in Loop: Header=BB17_19 Depth=1
	ldur	w5, [x29, #-4]                  // 4-byte Folded Reload
	ldr	w8, [sp, #4]                    // 4-byte Folded Reload
	adrp	x16, c_ftl_nand_planes_per_die
	adrp	x6, p_io_data_buf_0
	add	w5, w5, #1                      // =1
	cmp	w8, w5, uxth
	b.ls	.LBB17_37
.LBB17_19:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB17_23 Depth 2
                                        //       Child Loop BB17_27 Depth 3
                                        //     Child Loop BB17_33 Depth 2
	adrp	x17, c_ftl_nand_planes_num
	ldrh	w8, [x17, :lo12:c_ftl_nand_planes_num]
	cbz	w8, .LBB17_36
// %bb.20:                              //   in Loop: Header=BB17_19 Depth=1
	adrp	x18, p_plane_order_table
	mov	x8, xzr
	mov	w26, wzr
	and	w9, w5, #0xffff
	add	x18, x18, :lo12:p_plane_order_table
	adrp	x0, p_io_data_buf_1
	adrp	x1, c_ftl_nand_byte_pre_oob
	adrp	x2, g_totle_vendor_block
	adrp	x3, c_ftl_nand_max_vendor_blks
	adrp	x4, p_vendor_block_table
	b	.LBB17_23
.LBB17_21:                              //   in Loop: Header=BB17_23 Depth=2
	and	w12, w11, #0xffff
	and	w13, w10, #0xffff
	udiv	w13, w12, w13
	add	x14, x23, w13, uxtw #3
	ldr	x14, [x14, #32]
	msub	w10, w13, w10, w11
	lsr	w11, w10, #3
	and	x11, x11, #0x1ffc
	ldr	w11, [x14, x11]
	lsr	w10, w11, w10
	tbz	w10, #0, .LBB17_29
.LBB17_22:                              //   in Loop: Header=BB17_23 Depth=2
	ldrh	w10, [x17, :lo12:c_ftl_nand_planes_num]
	add	x8, x8, #1                      // =1
	cmp	x8, x10
	b.hs	.LBB17_30
.LBB17_23:                              //   Parent Loop BB17_19 Depth=1
                                        // =>  This Loop Header: Depth=2
                                        //       Child Loop BB17_27 Depth 3
	ldr	x10, [x25, :lo12:req_erase]
	lsl	x11, x8, #5
	tst	w19, #0xff
	str	wzr, [x10, x11]
	ldrb	w11, [x18, x8]
	ldrh	w12, [x16, :lo12:c_ftl_nand_planes_per_die]
	ldrh	w10, [x27, :lo12:c_ftl_nand_blks_per_die]
	and	w13, w11, #0xffff
	and	w14, w12, #0xffff
	mul	w15, w12, w20
	udiv	w13, w13, w14
	madd	w14, w10, w13, w15
	msub	w11, w13, w12, w11
	add	w11, w14, w11
	b.eq	.LBB17_21
// %bb.24:                              //   in Loop: Header=BB17_23 Depth=2
	ldrh	w12, [x2, :lo12:g_totle_vendor_block]
	cbz	w12, .LBB17_21
// %bb.25:                              //   in Loop: Header=BB17_23 Depth=2
	ldrh	w13, [x3, :lo12:c_ftl_nand_max_vendor_blks]
	cbz	w13, .LBB17_21
// %bb.26:                              //   in Loop: Header=BB17_23 Depth=2
	ldr	x12, [x4, :lo12:p_vendor_block_table]
	cmp	w13, #1                         // =1
	csinc	w13, w13, wzr, hi
.LBB17_27:                              //   Parent Loop BB17_19 Depth=1
                                        //     Parent Loop BB17_23 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	ldrh	w14, [x12]
	cmp	w14, w11, uxth
	b.eq	.LBB17_22
// %bb.28:                              //   in Loop: Header=BB17_27 Depth=3
	subs	x13, x13, #1                    // =1
	add	x12, x12, #2                    // =2
	b.ne	.LBB17_27
	b	.LBB17_21
.LBB17_29:                              //   in Loop: Header=BB17_23 Depth=2
	ldr	x10, [x25, :lo12:req_erase]
	add	w11, w9, w12, lsl #10
	and	x12, x26, #0xffff
	lsl	x12, x12, #5
	add	x10, x10, x12
	str	w11, [x10, #4]
	ldr	x10, [x25, :lo12:req_erase]
	ldr	x11, [x6, :lo12:p_io_data_buf_0]
	and	w13, w26, #0xffff
	add	w26, w26, #1                    // =1
	add	x10, x10, x12
	str	x11, [x10, #8]
	ldrh	w10, [x1, :lo12:c_ftl_nand_byte_pre_oob]
	ldr	x11, [x25, :lo12:req_erase]
	mul	w10, w10, w13
	ldr	x13, [x0, :lo12:p_io_data_buf_1]
	add	x11, x11, x12
	add	w12, w10, #3                    // =3
	cmp	w10, #0                         // =0
	csel	w10, w12, w10, lt
	asr	w10, w10, #2
	add	x10, x13, w10, sxtw #2
	str	x10, [x11, #16]
	b	.LBB17_22
.LBB17_30:                              //   in Loop: Header=BB17_19 Depth=1
	ands	w1, w26, #0xffff
	b.eq	.LBB17_36
// %bb.31:                              //   in Loop: Header=BB17_19 Depth=1
	ldr	x0, [x25, :lo12:req_erase]
	ldr	w2, [sp, #8]                    // 4-byte Folded Reload
	stur	w5, [x29, #-4]                  // 4-byte Folded Spill
	str	w1, [sp]                        // 4-byte Folded Spill
	bl	FlashProgPages
	mov	w8, w26
	and	x8, x8, #0xffff
	mov	x28, xzr
	lsl	x21, x8, #5
	b	.LBB17_33
.LBB17_32:                              //   in Loop: Header=BB17_33 Depth=2
	add	x28, x28, #32                   // =32
	cmp	x21, x28
	b.eq	.LBB17_18
.LBB17_33:                              //   Parent Loop BB17_19 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldr	x8, [x25, :lo12:req_erase]
	ldr	w9, [x8, x28]
	cbz	w9, .LBB17_32
// %bb.34:                              //   in Loop: Header=BB17_33 Depth=2
	add	x8, x8, x28
	ldr	w8, [x8, #4]
	ldrh	w9, [x27, :lo12:c_ftl_nand_blks_per_die]
	mov	w12, #1
	mov	x0, x22
	ubfx	w1, w8, #10, #16
	udiv	w2, w1, w9
	lsr	w10, w8, #10
	add	x8, x23, w2, uxtw #3
	ldr	x8, [x8, #32]
	msub	w9, w2, w9, w10
	ubfx	w10, w9, #3, #13
	and	x10, x10, #0x1ffc
	ldr	w11, [x8, x10]
	and	w3, w9, #0xffff
	lsl	w9, w12, w9
	add	w24, w24, #1                    // =1
	orr	w4, w9, w11
	str	w4, [x8, x10]
	bl	sftl_printk
	ldrh	w8, [x23, #6]
	add	w8, w8, #1                      // =1
	strh	w8, [x23, #6]
	b	.LBB17_32
.LBB17_35:
	mov	w24, wzr
.LBB17_36:
	and	w0, w24, #0xffff
	ldp	x20, x19, [sp, #96]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #80]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #64]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #48]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	add	sp, sp, #112                    // =112
	hint	#29
	ret
.LBB17_37:
	and	w8, w26, #0xffff
	cmp	w8, #1                          // =1
	csinc	w8, w26, wzr, hi
	and	x8, x8, #0xffff
	mov	x21, xzr
	mov	w12, #65535
	adrp	x23, gSysFreeQueue+6
	lsl	x26, x8, #5
	b	.LBB17_39
.LBB17_38:                              //   in Loop: Header=BB17_39 Depth=1
	add	x21, x21, #32                   // =32
	cmp	x26, x21
	b.eq	.LBB17_45
.LBB17_39:                              // =>This Inner Loop Header: Depth=1
	tst	w19, #0xff
	b.eq	.LBB17_38
// %bb.40:                              //   in Loop: Header=BB17_39 Depth=1
	ldr	x8, [x25, :lo12:req_erase]
	ldr	w9, [x8, x21]
	cbnz	w9, .LBB17_38
// %bb.41:                              //   in Loop: Header=BB17_39 Depth=1
	add	x9, x8, x21
	ldr	w10, [x9, #4]
	ubfx	w9, w10, #10, #16
	cbz	w9, .LBB17_38
// %bb.42:                              //   in Loop: Header=BB17_39 Depth=1
	cmp	w9, w12
	b.eq	.LBB17_38
// %bb.43:                              //   in Loop: Header=BB17_39 Depth=1
	ldrh	w11, [x23, :lo12:gSysFreeQueue+6]
	cmp	w11, #1024                      // =1024
	b.eq	.LBB17_38
// %bb.44:                              //   in Loop: Header=BB17_39 Depth=1
	lsr	w28, w10, #10
	ldrh	w11, [x27, :lo12:c_ftl_nand_blks_per_die]
	and	w10, w10, #0x3fffc00
	ldrh	w12, [x16, :lo12:c_ftl_nand_planes_per_die]
	str	w10, [x8, #4]
	ldr	x0, [x25, :lo12:req_erase]
	udiv	w8, w9, w11
	msub	w8, w8, w11, w9
	mov	w2, #1
	udiv	w22, w8, w12
	bl	FlashEraseBlocks
	adrp	x8, p_erase_count_table
	ldr	x8, [x8, :lo12:p_erase_count_table]
	lsl	x9, x22, #1
	adrp	x11, g_totle_sys_slc_erase_count
	adrp	x14, gSysFreeQueue
	ldrh	w10, [x8, x9]
	add	x14, x14, :lo12:gSysFreeQueue
	mov	w12, #65535
	adrp	x16, c_ftl_nand_planes_per_die
	add	w10, w10, #1                    // =1
	strh	w10, [x8, x9]
	ldr	w8, [x11, :lo12:g_totle_sys_slc_erase_count]
	ldrh	w9, [x14, #6]
	ldrh	w10, [x14, #4]
	add	w8, w8, #1                      // =1
	add	w9, w9, #1                      // =1
	str	w8, [x11, :lo12:g_totle_sys_slc_erase_count]
	add	x8, x14, x10, lsl #1
	add	w10, w10, #1                    // =1
	strh	w9, [x14, #6]
	strh	w28, [x8, #8]
	and	w8, w10, #0x3ff
	strh	w8, [x14, #4]
	b	.LBB17_38
.LBB17_45:
	and	w8, w20, #0xffff
	cmp	w8, #64                         // =64
	b.lo	.LBB17_47
// %bb.46:
	tst	w19, #0xff
	b.eq	.LBB17_36
.LBB17_47:
	ldr	x0, [x25, :lo12:req_erase]
	ldr	w2, [sp]                        // 4-byte Folded Reload
	bl	FlashEraseBlocks
	b	.LBB17_36
.Lfunc_end17:
	.size	FtlLowFormatEraseBlock, .Lfunc_end17-FtlLowFormatEraseBlock
                                        // -- End function
	.globl	V2P_block                       // -- Begin function V2P_block
	.p2align	2
	.type	V2P_block,@function
V2P_block:                              // @V2P_block
// %bb.0:
	hint	#25
	adrp	x8, c_ftl_nand_planes_per_die
	ldrh	w8, [x8, :lo12:c_ftl_nand_planes_per_die]
	adrp	x10, c_ftl_nand_blks_per_die
	ldrh	w10, [x10, :lo12:c_ftl_nand_blks_per_die]
	and	w9, w0, #0xffff
	and	w11, w8, #0xffff
	mul	w12, w8, w1
	udiv	w9, w9, w11
	madd	w10, w10, w9, w12
	msub	w8, w9, w8, w0
	add	w0, w10, w8
	hint	#29
	ret
.Lfunc_end18:
	.size	V2P_block, .Lfunc_end18-V2P_block
                                        // -- End function
	.globl	FtlBbmIsBadBlock                // -- Begin function FtlBbmIsBadBlock
	.p2align	2
	.type	FtlBbmIsBadBlock,@function
FtlBbmIsBadBlock:                       // @FtlBbmIsBadBlock
// %bb.0:
	hint	#25
	adrp	x8, c_ftl_nand_blks_per_die
	ldrh	w8, [x8, :lo12:c_ftl_nand_blks_per_die]
	and	w9, w0, #0xffff
	and	w10, w8, #0xffff
	udiv	w9, w9, w10
	adrp	x10, gBbtInfo
	add	x10, x10, :lo12:gBbtInfo
	add	x10, x10, w9, uxtw #3
	ldr	x10, [x10, #32]
	msub	w8, w9, w8, w0
	lsr	w9, w8, #3
	and	x9, x9, #0x1ffc
	ldr	w9, [x10, x9]
	hint	#29
	lsr	w8, w9, w8
	and	w0, w8, #0x1
	ret
.Lfunc_end19:
	.size	FtlBbmIsBadBlock, .Lfunc_end19-FtlBbmIsBadBlock
                                        // -- End function
	.globl	FlashEraseBlocks                // -- Begin function FlashEraseBlocks
	.p2align	2
	.type	FlashEraseBlocks,@function
FlashEraseBlocks:                       // @FlashEraseBlocks
// %bb.0:
	hint	#25
	sub	sp, sp, #112                    // =112
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	stp	x28, x27, [sp, #32]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #48]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #64]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #80]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #96]             // 16-byte Folded Spill
	add	x29, sp, #16                    // =16
	str	x0, [sp]                        // 8-byte Folded Spill
	cbz	w2, .LBB20_17
// %bb.1:
	adrp	x8, g_nand_phy_info+12
	ldrh	w23, [x8, :lo12:g_nand_phy_info+12]
	ldr	x19, [sp]                       // 8-byte Folded Reload
	adrp	x24, g_nand_phy_info+8
	mov	w22, w2
	add	x24, x24, :lo12:g_nand_phy_info+8
	adrp	x25, g_nand_ops+8
	adrp	x26, g_nand_phy_info+14
	lsl	w27, w23, #3
	mov	w28, #-1
	str	x22, [sp, #8]                   // 8-byte Folded Spill
	b	.LBB20_3
.LBB20_2:                               //   in Loop: Header=BB20_3 Depth=1
	subs	x22, x22, #1                    // =1
	add	x19, x19, #32                   // =32
	b.eq	.LBB20_17
.LBB20_3:                               // =>This Inner Loop Header: Depth=1
	ldrh	w8, [x24, #6]
	ldr	w9, [x19, #4]
	ldrh	w10, [x24]
	ldrh	w11, [x24, #2]
	cmp	w8, #4                          // =4
	cset	w12, eq
	ubfx	w8, w9, #10, #16
	lsr	w10, w10, w12
	lsl	w11, w11, w12
	udiv	w21, w8, w10
	and	w9, w9, #0x3ff
	and	w11, w11, #0xffff
	msub	w12, w10, w21, w8
	cmp	w10, w8
	madd	w20, w12, w11, w9
	b.ls	.LBB20_5
// %bb.4:                               //   in Loop: Header=BB20_3 Depth=1
	cmp	w20, w27
	b.lo	.LBB20_8
.LBB20_5:                               //   in Loop: Header=BB20_3 Depth=1
	ldr	x8, [x25, :lo12:g_nand_ops+8]
	mov	w0, w21
	mov	w1, w20
	blr	x8
	cmp	w0, #0                          // =0
	csetm	w8, ne
	str	w8, [x19]
	ldrh	w8, [x26, :lo12:g_nand_phy_info+14]
	cmp	w8, #4                          // =4
	b.ne	.LBB20_2
// %bb.6:                               //   in Loop: Header=BB20_3 Depth=1
	ldr	x8, [x25, :lo12:g_nand_ops+8]
	add	w1, w20, w23
	mov	w0, w21
	blr	x8
	cbz	w0, .LBB20_2
// %bb.7:                               //   in Loop: Header=BB20_3 Depth=1
	str	w28, [x19]
	b	.LBB20_2
.LBB20_8:
	ldr	x22, [sp]                       // 8-byte Folded Reload
	adrp	x23, .L.str.116
	adrp	x24, .L.str.124
	adrp	x25, .L.str.117
	adrp	x26, .L.str.120
	mov	x27, xzr
	add	x23, x23, :lo12:.L.str.116
	add	x24, x24, :lo12:.L.str.124
	add	x25, x25, :lo12:.L.str.117
	add	x26, x26, :lo12:.L.str.120
	b	.LBB20_10
.LBB20_9:                               //   in Loop: Header=BB20_10 Depth=1
	mov	x0, x26
	bl	sftl_printk
	add	x8, x22, x27, lsl #5
	ldr	x21, [x8, #16]
	adrp	x1, .L.str.125
	mov	x0, x23
	add	x1, x1, :lo12:.L.str.125
	mov	x2, x21
	mov	w3, wzr
	bl	sftl_printk
	ldr	w1, [x21]
	mov	x0, x25
	bl	sftl_printk
	ldr	w1, [x21, #4]
	mov	x0, x25
	bl	sftl_printk
	ldr	w1, [x21, #8]
	mov	x0, x25
	bl	sftl_printk
	ldr	w1, [x21, #12]
	mov	x0, x25
	bl	sftl_printk
	mov	x0, x26
	bl	sftl_printk
	ldr	x8, [sp, #8]                    // 8-byte Folded Reload
	add	x27, x27, #1                    // =1
	cmp	x27, x8
	b.eq	.LBB20_16
.LBB20_10:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB20_13 Depth 2
	adrp	x0, .L.str.123
	adrp	x1, .L__func__.FlashEraseBlocks
	add	x19, x22, x27, lsl #5
	mov	w8, #-1
	add	x0, x0, :lo12:.L.str.123
	add	x1, x1, :lo12:.L__func__.FlashEraseBlocks
	mov	w2, w20
	str	w8, [x19]
	bl	sftl_printk
	ldr	x21, [x19, #8]
	mov	x28, xzr
	mov	w19, wzr
	b	.LBB20_13
.LBB20_11:                              //   in Loop: Header=BB20_13 Depth=2
	ldr	w1, [x21, x28]
	mov	x0, x25
	bl	sftl_printk
	add	w19, w19, #1                    // =1
	cmp	w19, #16                        // =16
	b.hs	.LBB20_15
.LBB20_12:                              //   in Loop: Header=BB20_13 Depth=2
	add	x28, x28, #4                    // =4
	cmp	x28, #64                        // =64
	b.eq	.LBB20_9
.LBB20_13:                              //   Parent Loop BB20_10 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	cbnz	w19, .LBB20_11
// %bb.14:                              //   in Loop: Header=BB20_13 Depth=2
	mov	x0, x23
	mov	x1, x24
	mov	x2, x21
	mov	w3, w28
	bl	sftl_printk
	b	.LBB20_11
.LBB20_15:                              //   in Loop: Header=BB20_13 Depth=2
	mov	x0, x26
	bl	sftl_printk
	mov	w19, wzr
	b	.LBB20_12
.LBB20_16:
	bl	dump_stack
.LBB20_17:
	ldp	x20, x19, [sp, #96]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #80]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #64]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #48]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	add	sp, sp, #112                    // =112
	hint	#29
	ret
.Lfunc_end20:
	.size	FlashEraseBlocks, .Lfunc_end20-FlashEraseBlocks
                                        // -- End function
	.globl	FtlBbmMapBadBlock               // -- Begin function FtlBbmMapBadBlock
	.p2align	2
	.type	FtlBbmMapBadBlock,@function
FtlBbmMapBadBlock:                      // @FtlBbmMapBadBlock
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	adrp	x8, c_ftl_nand_blks_per_die
	ldrh	w8, [x8, :lo12:c_ftl_nand_blks_per_die]
	str	x19, [sp, #16]                  // 8-byte Folded Spill
	and	w1, w0, #0xffff
	adrp	x19, gBbtInfo
	udiv	w2, w1, w8
	add	x19, x19, :lo12:gBbtInfo
	add	x9, x19, w2, uxtw #3
	ldr	x9, [x9, #32]
	msub	w8, w2, w8, w0
	ubfx	w10, w8, #3, #13
	and	x10, x10, #0x1ffc
	ldr	w11, [x9, x10]
	mov	w12, #1
	lsl	w12, w12, w8
	adrp	x0, .L.str.82
	and	w3, w8, #0xffff
	orr	w4, w12, w11
	add	x0, x0, :lo12:.L.str.82
	mov	x29, sp
	str	w4, [x9, x10]
	bl	sftl_printk
	ldrh	w8, [x19, #6]
	mov	w0, wzr
	add	w8, w8, #1                      // =1
	strh	w8, [x19, #6]
	ldr	x19, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end21:
	.size	FtlBbmMapBadBlock, .Lfunc_end21-FtlBbmMapBadBlock
                                        // -- End function
	.globl	FlashProgPages                  // -- Begin function FlashProgPages
	.p2align	2
	.type	FlashProgPages,@function
FlashProgPages:                         // @FlashProgPages
// %bb.0:
	hint	#25
	sub	sp, sp, #160                    // =160
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	stp	x29, x30, [sp, #64]             // 16-byte Folded Spill
	add	x29, sp, #64                    // =64
	stp	x28, x27, [sp, #80]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #96]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #112]            // 16-byte Folded Spill
	stp	x22, x21, [sp, #128]            // 16-byte Folded Spill
	stp	x20, x19, [sp, #144]            // 16-byte Folded Spill
	stur	x8, [x29, #-8]
	cbz	w1, .LBB22_41
// %bb.1:
	adrp	x8, g_nand_phy_info+12
	ldrh	w8, [x8, :lo12:g_nand_phy_info+12]
	mov	w21, w1
	adrp	x26, .L.str.78
	adrp	x22, .L__func__.FlashProgPages
	adrp	x24, g_nand_phy_info+8
	mov	x19, x0
	add	x27, x0, #16                    // =16
	add	x26, x26, :lo12:.L.str.78
	add	x22, x22, :lo12:.L__func__.FlashProgPages
	add	x24, x24, :lo12:g_nand_phy_info+8
	adrp	x28, sftl_nand_check_buf
	stp	w1, w8, [sp, #4]                // 8-byte Folded Spill
	lsl	w8, w8, #3
	mov	x20, x21
	str	w8, [sp, #12]                   // 4-byte Folded Spill
	str	x21, [sp, #16]                  // 8-byte Folded Spill
	b	.LBB22_4
.LBB22_2:                               //   in Loop: Header=BB22_4 Depth=1
	mov	w8, #-1
	stur	w8, [x27, #-16]
.LBB22_3:                               //   in Loop: Header=BB22_4 Depth=1
	adrp	x24, g_nand_phy_info+8
	subs	x20, x20, #1                    // =1
	add	x27, x27, #32                   // =32
	add	x24, x24, :lo12:g_nand_phy_info+8
	b.eq	.LBB22_16
.LBB22_4:                               // =>This Inner Loop Header: Depth=1
	ldur	x8, [x27, #-8]
	cbz	x8, .LBB22_6
// %bb.5:                               //   in Loop: Header=BB22_4 Depth=1
	ldr	x8, [x27]
	cbnz	x8, .LBB22_7
.LBB22_6:                               //   in Loop: Header=BB22_4 Depth=1
	mov	w2, #148
	mov	x0, x26
	mov	x1, x22
	bl	sftl_printk
.LBB22_7:                               //   in Loop: Header=BB22_4 Depth=1
	ldrh	w9, [x24, #6]
	ldrh	w11, [x24]
	ldur	w8, [x27, #-12]
	cmp	w9, #4                          // =4
	cset	w10, eq
	lsr	w9, w11, w10
	ubfx	w11, w8, #10, #16
	udiv	w23, w11, w9
	cmp	w23, #4                         // =4
	b.hs	.LBB22_2
// %bb.8:                               //   in Loop: Header=BB22_4 Depth=1
	ldrh	w12, [x24, #2]
	msub	w11, w9, w23, w11
	and	w13, w8, #0x3ff
	lsr	w8, w8, #10
	lsl	w10, w12, w10
	and	w10, w10, #0xffff
	cmp	w9, w8, uxth
	madd	w24, w11, w10, w13
	b.ls	.LBB22_10
// %bb.9:                               //   in Loop: Header=BB22_4 Depth=1
	ldr	w8, [sp, #12]                   // 4-byte Folded Reload
	cmp	w24, w8
	b.lo	.LBB22_32
.LBB22_10:                              //   in Loop: Header=BB22_4 Depth=1
	ldur	x25, [x27, #-8]
	tst	x25, #0x3f
	b.eq	.LBB22_14
// %bb.11:                              //   in Loop: Header=BB22_4 Depth=1
	mov	x8, x28
	mov	x28, x26
	ldr	x26, [x8, :lo12:sftl_nand_check_buf]
	mov	x21, x22
	mov	x22, x8
	cmp	x25, x26
	b.eq	.LBB22_13
// %bb.12:                              //   in Loop: Header=BB22_4 Depth=1
	adrp	x8, c_ftl_nand_sec_pre_page
	ldrh	w8, [x8, :lo12:c_ftl_nand_sec_pre_page]
	mov	x0, x26
	mov	x1, x25
	lsl	x2, x8, #9
	bl	memcpy
	mov	x25, x26
.LBB22_13:                              //   in Loop: Header=BB22_4 Depth=1
	mov	x26, x28
	mov	x28, x22
	mov	x22, x21
	ldr	x21, [sp, #16]                  // 8-byte Folded Reload
.LBB22_14:                              //   in Loop: Header=BB22_4 Depth=1
	adrp	x8, g_nand_ops+16
	ldr	x8, [x8, :lo12:g_nand_ops+16]
	ldr	x3, [x27]
	mov	w0, w23
	mov	w1, w24
	mov	x2, x25
	blr	x8
	cmp	w0, #0                          // =0
	csetm	w8, ne
	stur	w8, [x27, #-16]
	adrp	x8, g_nand_phy_info+14
	ldrh	w8, [x8, :lo12:g_nand_phy_info+14]
	cmp	w8, #4                          // =4
	b.ne	.LBB22_3
// %bb.15:                              //   in Loop: Header=BB22_4 Depth=1
	ldr	x8, [x27]
	adrp	x9, g_nand_ops+16
	ldr	w10, [sp, #8]                   // 4-byte Folded Reload
	ldr	x9, [x9, :lo12:g_nand_ops+16]
	add	x2, x25, #2048                  // =2048
	add	x3, x8, #8                      // =8
	add	w1, w24, w10
	mov	w0, w23
	blr	x9
	cbnz	w0, .LBB22_2
	b	.LBB22_3
.LBB22_16:
	ldr	w8, [sp, #4]                    // 4-byte Folded Reload
	cbz	w8, .LBB22_41
// %bb.17:
	adrp	x24, .L.str.126
	adrp	x22, .L.str.129
	adrp	x20, .L.str.128
	adrp	x26, .L.str.127
	adrp	x23, sftl_nand_check_spare_buf
	add	x24, x24, :lo12:.L.str.126
	add	x22, x22, :lo12:.L.str.129
	mov	w25, #-1
	add	x20, x20, :lo12:.L.str.128
	add	x26, x26, :lo12:.L.str.127
	b	.LBB22_20
.LBB22_18:                              //   in Loop: Header=BB22_20 Depth=1
	ldr	w1, [x19, #4]
	ldr	w3, [x8, #4]
	ldr	w5, [x9, #4]
	mov	x0, x22
	bl	sftl_printk
	str	w25, [x19]
.LBB22_19:                              //   in Loop: Header=BB22_20 Depth=1
	subs	x21, x21, #1                    // =1
	add	x19, x19, #32                   // =32
	b.eq	.LBB22_41
.LBB22_20:                              // =>This Inner Loop Header: Depth=1
	ldr	x8, [x28, :lo12:sftl_nand_check_buf]
	add	x0, sp, #24                     // =24
	mov	w1, #1
	str	wzr, [x8]
	ldr	x8, [x23, :lo12:sftl_nand_check_spare_buf]
	str	wzr, [x8]
	ldr	w8, [x19, #4]
	ldr	x9, [x28, :lo12:sftl_nand_check_buf]
	ldr	x10, [x23, :lo12:sftl_nand_check_spare_buf]
	str	w8, [sp, #28]
	stp	x9, x10, [sp, #32]
	bl	FlashReadPages
	ldr	w27, [sp, #24]
	cmn	w27, #1                         // =1
	b.eq	.LBB22_23
// %bb.21:                              //   in Loop: Header=BB22_20 Depth=1
	cmp	w27, #256                       // =256
	b.ne	.LBB22_25
// %bb.22:                              //   in Loop: Header=BB22_20 Depth=1
	mov	x0, x26
	b	.LBB22_24
.LBB22_23:                              //   in Loop: Header=BB22_20 Depth=1
	mov	x0, x24
.LBB22_24:                              //   in Loop: Header=BB22_20 Depth=1
	ldr	w1, [x19, #4]
	bl	sftl_printk
	str	w27, [x19]
.LBB22_25:                              //   in Loop: Header=BB22_20 Depth=1
	ldr	x8, [x19, #16]
	cbz	x8, .LBB22_29
// %bb.26:                              //   in Loop: Header=BB22_20 Depth=1
	ldr	x9, [x23, :lo12:sftl_nand_check_spare_buf]
	ldr	w2, [x8]
	ldr	w4, [x9]
	cmp	w2, w4
	b.ne	.LBB22_28
// %bb.27:                              //   in Loop: Header=BB22_20 Depth=1
	ldr	w10, [x8, #8]
	ldr	w11, [x9, #8]
	cmp	w10, w11
	b.eq	.LBB22_29
.LBB22_28:                              //   in Loop: Header=BB22_20 Depth=1
	ldr	w1, [x19, #4]
	ldr	w3, [x8, #4]
	ldr	w5, [x9, #4]
	mov	x0, x20
	bl	sftl_printk
	str	w25, [x19]
.LBB22_29:                              //   in Loop: Header=BB22_20 Depth=1
	ldr	x8, [x19, #8]
	cbz	x8, .LBB22_19
// %bb.30:                              //   in Loop: Header=BB22_20 Depth=1
	ldr	x9, [x28, :lo12:sftl_nand_check_buf]
	ldr	w2, [x8]
	ldr	w4, [x9]
	cmp	w2, w4
	b.ne	.LBB22_18
// %bb.31:                              //   in Loop: Header=BB22_20 Depth=1
	ldr	w10, [x8, #2048]
	ldr	w11, [x9, #2048]
	cmp	w10, w11
	b.ne	.LBB22_18
	b	.LBB22_19
.LBB22_32:
	adrp	x22, .L.str.116
	adrp	x23, .L.str.124
	adrp	x24, .L.str.117
	adrp	x25, .L.str.120
	mov	x20, xzr
	add	x22, x22, :lo12:.L.str.116
	add	x23, x23, :lo12:.L.str.124
	add	x24, x24, :lo12:.L.str.117
	add	x25, x25, :lo12:.L.str.120
	b	.LBB22_34
.LBB22_33:                              //   in Loop: Header=BB22_34 Depth=1
	mov	x0, x25
	bl	sftl_printk
	add	x8, x19, x20, lsl #5
	ldr	x27, [x8, #16]
	adrp	x1, .L.str.125
	mov	x0, x22
	add	x1, x1, :lo12:.L.str.125
	mov	x2, x27
	mov	w3, wzr
	bl	sftl_printk
	ldr	w1, [x27]
	mov	x0, x24
	bl	sftl_printk
	ldr	w1, [x27, #4]
	mov	x0, x24
	bl	sftl_printk
	ldr	w1, [x27, #8]
	mov	x0, x24
	bl	sftl_printk
	ldr	w1, [x27, #12]
	mov	x0, x24
	bl	sftl_printk
	mov	x0, x25
	bl	sftl_printk
	add	x20, x20, #1                    // =1
	cmp	x20, x21
	b.eq	.LBB22_40
.LBB22_34:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB22_37 Depth 2
	add	x26, x19, x20, lsl #5
	ldr	w2, [x26, #4]
	adrp	x0, .L.str.123
	adrp	x1, .L__func__.FlashProgPages
	mov	w8, #-1
	add	x0, x0, :lo12:.L.str.123
	add	x1, x1, :lo12:.L__func__.FlashProgPages
	str	w8, [x26]
	bl	sftl_printk
	ldr	x28, [x26, #8]
	mov	x27, xzr
	mov	w26, wzr
	b	.LBB22_37
.LBB22_35:                              //   in Loop: Header=BB22_37 Depth=2
	ldr	w1, [x28, x27]
	mov	x0, x24
	bl	sftl_printk
	add	w26, w26, #1                    // =1
	cmp	w26, #16                        // =16
	b.hs	.LBB22_39
.LBB22_36:                              //   in Loop: Header=BB22_37 Depth=2
	add	x27, x27, #4                    // =4
	cmp	x27, #64                        // =64
	b.eq	.LBB22_33
.LBB22_37:                              //   Parent Loop BB22_34 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	cbnz	w26, .LBB22_35
// %bb.38:                              //   in Loop: Header=BB22_37 Depth=2
	mov	x0, x22
	mov	x1, x23
	mov	x2, x28
	mov	w3, w27
	bl	sftl_printk
	b	.LBB22_35
.LBB22_39:                              //   in Loop: Header=BB22_37 Depth=2
	mov	x0, x25
	bl	sftl_printk
	mov	w26, wzr
	b	.LBB22_36
.LBB22_40:
	bl	dump_stack
.LBB22_41:
	adrp	x9, __stack_chk_guard
	ldur	x8, [x29, #-8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB22_43
// %bb.42:
	ldp	x20, x19, [sp, #144]            // 16-byte Folded Reload
	ldp	x22, x21, [sp, #128]            // 16-byte Folded Reload
	ldp	x24, x23, [sp, #112]            // 16-byte Folded Reload
	ldp	x26, x25, [sp, #96]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #80]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #64]             // 16-byte Folded Reload
	mov	w0, wzr
	add	sp, sp, #160                    // =160
	hint	#29
	ret
.LBB22_43:
	bl	__stack_chk_fail
.Lfunc_end22:
	.size	FlashProgPages, .Lfunc_end22-FlashProgPages
                                        // -- End function
	.globl	FtlFreeSysBlkQueueIn            // -- Begin function FtlFreeSysBlkQueueIn
	.p2align	2
	.type	FtlFreeSysBlkQueueIn,@function
FtlFreeSysBlkQueueIn:                   // @FtlFreeSysBlkQueueIn
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-48]!           // 16-byte Folded Spill
	ands	w8, w0, #0xffff
	str	x21, [sp, #16]                  // 8-byte Folded Spill
	stp	x20, x19, [sp, #32]             // 16-byte Folded Spill
	mov	x29, sp
	b.eq	.LBB23_6
// %bb.1:
	mov	w9, #65535
	cmp	w8, w9
	b.eq	.LBB23_6
// %bb.2:
	adrp	x20, gSysFreeQueue+6
	ldrh	w9, [x20, :lo12:gSysFreeQueue+6]
	cmp	w9, #1024                       // =1024
	b.eq	.LBB23_6
// %bb.3:
	mov	w19, w0
	tst	w1, #0xffff
	b.eq	.LBB23_5
// %bb.4:
	adrp	x11, req_erase
	ldr	x12, [x11, :lo12:req_erase]
	adrp	x9, c_ftl_nand_blks_per_die
	ldrh	w9, [x9, :lo12:c_ftl_nand_blks_per_die]
	adrp	x13, c_ftl_nand_planes_per_die
	lsl	w8, w8, #10
	ldrh	w13, [x13, :lo12:c_ftl_nand_planes_per_die]
	str	w8, [x12, #4]
	ldr	x0, [x11, :lo12:req_erase]
	and	w10, w19, #0xffff
	udiv	w8, w10, w9
	msub	w8, w8, w9, w10
	mov	w2, #1
	udiv	w21, w8, w13
	bl	FlashEraseBlocks
	adrp	x8, p_erase_count_table
	ldr	x8, [x8, :lo12:p_erase_count_table]
	lsl	x9, x21, #1
	ldrh	w10, [x8, x9]
	add	w10, w10, #1                    // =1
	strh	w10, [x8, x9]
	adrp	x8, g_totle_sys_slc_erase_count
	ldr	w10, [x8, :lo12:g_totle_sys_slc_erase_count]
	ldrh	w9, [x20, :lo12:gSysFreeQueue+6]
	add	w10, w10, #1                    // =1
	str	w10, [x8, :lo12:g_totle_sys_slc_erase_count]
.LBB23_5:
	adrp	x8, gSysFreeQueue
	add	x8, x8, :lo12:gSysFreeQueue
	ldrh	w10, [x8, #4]
	add	w9, w9, #1                      // =1
	strh	w9, [x8, #6]
	add	x9, x8, x10, lsl #1
	add	w10, w10, #1                    // =1
	strh	w19, [x9, #8]
	and	w9, w10, #0x3ff
	strh	w9, [x8, #4]
.LBB23_6:
	ldp	x20, x19, [sp, #32]             // 16-byte Folded Reload
	ldr	x21, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #48             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end23:
	.size	FtlFreeSysBlkQueueIn, .Lfunc_end23-FtlFreeSysBlkQueueIn
                                        // -- End function
	.globl	ftl_low_format                  // -- Begin function ftl_low_format
	.p2align	2
	.type	ftl_low_format,@function
ftl_low_format:                         // @ftl_low_format
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-96]!           // 16-byte Folded Spill
	stp	x26, x25, [sp, #32]             // 16-byte Folded Spill
	adrp	x8, g_GlobalDataVersion
	adrp	x25, c_ftl_nand_max_sys_blks
	str	wzr, [x8, :lo12:g_GlobalDataVersion]
	ldr	w8, [x25, :lo12:c_ftl_nand_max_sys_blks]
	stp	x22, x21, [sp, #64]             // 16-byte Folded Spill
	adrp	x21, gSysFreeQueue
	add	x21, x21, :lo12:gSysFreeQueue
	adrp	x10, g_GlobalSysVersion
	adrp	x9, g_totle_avg_erase_count
	add	x0, x21, #8                     // =8
	mov	w2, #2048
	mov	w1, wzr
	stp	x28, x27, [sp, #16]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #48]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #80]             // 16-byte Folded Spill
	mov	x29, sp
	str	wzr, [x10, :lo12:g_GlobalSysVersion]
	str	wzr, [x9, :lo12:g_totle_avg_erase_count]
	stur	wzr, [x21, #2]
	strh	wzr, [x21, #6]
	strh	w8, [x21]
	bl	memset
	bl	FtlLoadBbt
	cbz	w0, .LBB24_2
// %bb.1:
	bl	FtlMakeBbt
.LBB24_2:
	adrp	x22, c_ftl_nand_sec_pre_page
	ldrh	w8, [x22, :lo12:c_ftl_nand_sec_pre_page]
	cbz	w8, .LBB24_5
// %bb.3:
	mov	w11, #23752
	mov	w8, wzr
	adrp	x9, p_io_data_buf_0
	adrp	x10, p_io_data_buf_1
	movk	w11, #2575, lsl #16
.LBB24_4:                               // =>This Inner Loop Header: Depth=1
	ldr	x13, [x9, :lo12:p_io_data_buf_0]
	mvn	w12, w8
	and	x14, x8, #0xffff
	orr	w12, w8, w12, lsl #16
	lsl	x14, x14, #2
	str	w12, [x13, x14]
	ldr	x12, [x10, :lo12:p_io_data_buf_1]
	add	w8, w8, #1                      // =1
	and	w8, w8, #0xffff
	str	w11, [x12, x14]
	ldrh	w12, [x22, :lo12:c_ftl_nand_sec_pre_page]
	cmp	w8, w12, lsl #7
	b.lo	.LBB24_4
.LBB24_5:
	adrp	x24, c_ftl_nand_data_blks_per_plane
	adrp	x23, c_ftl_nand_blk_pre_plane
	ldrh	w19, [x24, :lo12:c_ftl_nand_data_blks_per_plane]
	ldrh	w28, [x23, :lo12:c_ftl_nand_blk_pre_plane]
	cmp	w19, w28
	b.hs	.LBB24_8
// %bb.6:
	mov	w20, wzr
.LBB24_7:                               // =>This Inner Loop Header: Depth=1
	mov	w1, #1
	mov	w0, w19
	bl	FtlLowFormatEraseBlock
	ldrh	w28, [x23, :lo12:c_ftl_nand_blk_pre_plane]
	add	w8, w0, w20
	add	w19, w19, #1                    // =1
	and	w20, w8, #0xffff
	cmp	w28, w19, uxth
	b.hi	.LBB24_7
	b	.LBB24_9
.LBB24_8:
	mov	w20, wzr
.LBB24_9:
	adrp	x26, c_ftl_nand_planes_num
	ldrh	w8, [x26, :lo12:c_ftl_nand_planes_num]
	sub	w9, w20, #3                     // =3
	adrp	x27, c_ftl_nand_max_data_blks
	cmp	w9, w8, lsl #1
	b.ge	.LBB24_14
.LBB24_10:
	ldrh	w8, [x24, :lo12:c_ftl_nand_data_blks_per_plane]
	cbz	w8, .LBB24_16
// %bb.11:
	mov	w25, wzr
	mov	w19, wzr
.LBB24_12:                              // =>This Inner Loop Header: Depth=1
	mov	w0, w19
	mov	w1, wzr
	bl	FtlLowFormatEraseBlock
	ldrh	w8, [x24, :lo12:c_ftl_nand_data_blks_per_plane]
	add	w9, w0, w25
	add	w19, w19, #1                    // =1
	and	w25, w9, #0xffff
	cmp	w8, w19, uxth
	b.hi	.LBB24_12
// %bb.13:
	ldrh	w28, [x23, :lo12:c_ftl_nand_blk_pre_plane]
	b	.LBB24_17
.LBB24_14:
	adrp	x9, c_ftl_nand_init_sys_blks_per_plane
	ldrh	w9, [x9, :lo12:c_ftl_nand_init_sys_blks_per_plane]
	udiv	w10, w20, w8
	mov	w11, #24
	add	x0, x21, #8                     // =8
	add	w9, w10, w9
	and	w10, w9, #0xffff
	cmp	w10, #24                        // =24
	csel	w9, w9, w11, hi
	adrp	x10, c_ftl_nand_sys_blks_per_plane
	and	w11, w9, #0xffff
	str	w11, [x10, :lo12:c_ftl_nand_sys_blks_per_plane]
	adrp	x10, c_ftl_nand_totle_phy_blks
	ldr	w10, [x10, :lo12:c_ftl_nand_totle_phy_blks]
	mul	w8, w11, w8
	sub	w19, w28, w9
	mov	w2, #2048
	sub	w9, w10, w8
	mov	w1, wzr
	stur	wzr, [x21, #2]
	strh	wzr, [x21, #6]
	strh	w19, [x24, :lo12:c_ftl_nand_data_blks_per_plane]
	str	w8, [x25, :lo12:c_ftl_nand_max_sys_blks]
	str	w9, [x27, :lo12:c_ftl_nand_max_data_blks]
	strh	w8, [x21]
	bl	memset
	cmp	w28, w19, uxth
	b.ls	.LBB24_10
.LBB24_15:                              // =>This Inner Loop Header: Depth=1
	mov	w1, #1
	mov	w0, w19
	bl	FtlLowFormatEraseBlock
	ldrh	w28, [x23, :lo12:c_ftl_nand_blk_pre_plane]
	add	w19, w19, #1                    // =1
	cmp	w28, w19, uxth
	b.hi	.LBB24_15
	b	.LBB24_10
.LBB24_16:
	mov	w25, wzr
.LBB24_17:
	ldrh	w8, [x26, :lo12:c_ftl_nand_planes_num]
	ldr	w10, [x27, :lo12:c_ftl_nand_max_data_blks]
	adrp	x9, g_cur_erase_blk
	str	w28, [x9, :lo12:g_cur_erase_blk]
	add	w9, w8, w8, lsl #1
	cmp	w25, w9, lsl #3
	udiv	w9, w10, w8
	b.ls	.LBB24_19
// %bb.18:
	sub	w10, w10, w25
	udiv	w11, w10, w8
	lsr	w10, w11, #5
	add	w10, w10, #24                   // =24
	b	.LBB24_20
.LBB24_19:
	lsr	w10, w9, #5
	add	w10, w10, #36                   // =36
	mov	w11, w9
.LBB24_20:
	adrp	x12, c_ftl_nand_ext_blk_pre_plane
	ldrh	w13, [x12, :lo12:c_ftl_nand_ext_blk_pre_plane]
	adrp	x12, c_ftl_nand_data_op_blks_per_plane
	strh	w10, [x12, :lo12:c_ftl_nand_data_op_blks_per_plane]
	cbz	w13, .LBB24_23
// %bb.21:
	mul	w14, w13, w8
	add	w10, w10, w13, lsr #1
	cmp	w14, w25
	adrp	x25, g_GlobalSysVersion
	strh	w10, [x12, :lo12:c_ftl_nand_data_op_blks_per_plane]
	b.le	.LBB24_24
// %bb.22:
	add	w10, w13, w9, lsr #5
	add	w10, w10, #32                   // =32
	mov	w11, w9
	strh	w10, [x12, :lo12:c_ftl_nand_data_op_blks_per_plane]
	b	.LBB24_24
.LBB24_23:
	adrp	x25, g_GlobalSysVersion
.LBB24_24:
	sub	w9, w11, w10, uxth
	adrp	x10, c_ftl_nand_page_pre_blk
	ldrh	w10, [x10, :lo12:c_ftl_nand_page_pre_blk]
	ldrh	w12, [x22, :lo12:c_ftl_nand_sec_pre_page]
	adrp	x24, g_MaxLbn
	mul	w8, w9, w8
	adrp	x11, g_MaxLpn
	str	w8, [x24, :lo12:g_MaxLbn]
	mul	w8, w8, w10
	str	w8, [x11, :lo12:g_MaxLpn]
	mul	w8, w8, w12
	adrp	x9, g_MaxLbaSector
	str	w8, [x9, :lo12:g_MaxLbaSector]
	bl	FtlBbmTblFlush
	adrp	x22, p_valid_page_count_table
	ldrh	w8, [x23, :lo12:c_ftl_nand_blk_pre_plane]
	ldr	x0, [x22, :lo12:p_valid_page_count_table]
	mov	w1, wzr
	lsl	x2, x8, #1
	bl	memset
	adrp	x9, g_gc_superblock
	adrp	x19, g_active_superblock
	adrp	x8, g_VaildLpn
	add	x9, x9, :lo12:g_gc_superblock
	mov	w20, #65535
	add	x19, x19, :lo12:g_active_superblock
	mov	w23, #1
	str	wzr, [x8, :lo12:g_VaildLpn]
	str	w20, [x9]
	strb	wzr, [x9, #6]
	strb	wzr, [x9, #8]
	strb	wzr, [x19, #6]
	str	wzr, [x19]
	strb	w23, [x19, #8]
.LBB24_25:                              // =>This Inner Loop Header: Depth=1
	mov	x0, x19
	bl	make_superblock
	ldrb	w8, [x19, #7]
	cbnz	w8, .LBB24_27
// %bb.26:                              //   in Loop: Header=BB24_25 Depth=1
	ldr	x8, [x22, :lo12:p_valid_page_count_table]
	ldrh	w9, [x19]
	strh	w20, [x8, x9, lsl #1]
	ldrh	w8, [x19]
	add	w8, w8, #1                      // =1
	strh	w8, [x19]
	b	.LBB24_25
.LBB24_27:
	ldr	w8, [x25, :lo12:g_GlobalSysVersion]
	ldrh	w9, [x19, #4]
	ldr	x10, [x22, :lo12:p_valid_page_count_table]
	ldrh	w11, [x19]
	str	w8, [x19, #12]
	add	w8, w8, #1                      // =1
	str	w8, [x25, :lo12:g_GlobalSysVersion]
	strh	w9, [x10, x11, lsl #1]
	ldrh	w8, [x19]
	adrp	x19, g_buffer_superblock
	add	x19, x19, :lo12:g_buffer_superblock
	mov	x0, x19
	add	w8, w8, #1                      // =1
	strh	wzr, [x19, #2]
	strb	wzr, [x19, #6]
	strh	w8, [x19]
	strb	w23, [x19, #8]
	bl	make_superblock
	ldrb	w8, [x19, #7]
	cbnz	w8, .LBB24_30
// %bb.28:
	mov	w20, #65535
.LBB24_29:                              // =>This Inner Loop Header: Depth=1
	ldr	x8, [x22, :lo12:p_valid_page_count_table]
	ldrh	w9, [x19]
	mov	x0, x19
	strh	w20, [x8, x9, lsl #1]
	ldrh	w8, [x19]
	add	w8, w8, #1                      // =1
	strh	w8, [x19]
	bl	make_superblock
	ldrb	w8, [x19, #7]
	cbz	w8, .LBB24_29
.LBB24_30:
	ldr	w8, [x25, :lo12:g_GlobalSysVersion]
	ldrh	w9, [x19, #4]
	ldr	x10, [x22, :lo12:p_valid_page_count_table]
	ldrh	w11, [x19]
	str	w8, [x19, #12]
	add	w8, w8, #1                      // =1
	str	w8, [x25, :lo12:g_GlobalSysVersion]
	strh	w9, [x10, x11, lsl #1]
	adrp	x22, gSysFreeQueue+6
	ldrh	w8, [x22, :lo12:gSysFreeQueue+6]
	adrp	x9, g_gc_temp_superblock
	mov	w20, #65535
	strh	w20, [x9, :lo12:g_gc_temp_superblock]
	cbz	w8, .LBB24_36
// %bb.31:
	ldrh	w9, [x21, #2]
	sub	w8, w8, #1                      // =1
	mov	w2, #1
	add	x10, x21, x9, lsl #1
	ldrh	w19, [x10, #8]
	strh	w8, [x21, #6]
	adrp	x8, req_erase
	ldr	x10, [x8, :lo12:req_erase]
	add	w9, w9, #1                      // =1
	and	w9, w9, #0x3ff
	strh	w9, [x21, #2]
	lsl	w9, w19, #10
	str	w9, [x10, #4]
	ldr	x0, [x8, :lo12:req_erase]
	bl	FlashEraseBlocks
	adrp	x8, g_totle_sys_slc_erase_count
	ldr	w9, [x8, :lo12:g_totle_sys_slc_erase_count]
	cmp	w19, w20
	add	w9, w9, #1                      // =1
	str	w9, [x8, :lo12:g_totle_sys_slc_erase_count]
	b.eq	.LBB24_37
// %bb.32:
	cbz	w19, .LBB24_37
// %bb.33:
	ldr	w10, [x24, :lo12:g_MaxLbn]
	ldr	w11, [x25, :lo12:g_GlobalSysVersion]
	adrp	x8, gSysInfo
	add	x8, x8, :lo12:gSysInfo
	mov	w9, #-65536
	strh	w19, [x8]
	stur	w9, [x8, #2]
	strh	w10, [x8, #6]
	str	w11, [x8, #8]
	add	w8, w11, #1                     // =1
	str	w8, [x25, :lo12:g_GlobalSysVersion]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	w0, .LBB24_35
// %bb.34:
	adrp	x8, gFtlInitStatus
	mov	w9, #1
	str	w9, [x8, :lo12:gFtlInitStatus]
.LBB24_35:
	ldp	x20, x19, [sp, #80]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #64]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #48]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #32]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #96             // 16-byte Folded Reload
	hint	#29
	ret
.LBB24_36:
	mov	w19, #65535
.LBB24_37:
	ldrh	w2, [x22, :lo12:gSysFreeQueue+6]
	adrp	x0, .L.str.86
	add	x0, x0, :lo12:.L.str.86
	mov	w1, w19
	bl	sftl_printk
.LBB24_38:                              // =>This Inner Loop Header: Depth=1
	b	.LBB24_38
.Lfunc_end24:
	.size	ftl_low_format, .Lfunc_end24-ftl_low_format
                                        // -- End function
	.globl	FtlFreeSysBlkQueueInit          // -- Begin function FtlFreeSysBlkQueueInit
	.p2align	2
	.type	FtlFreeSysBlkQueueInit,@function
FtlFreeSysBlkQueueInit:                 // @FtlFreeSysBlkQueueInit
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	adrp	x8, gSysFreeQueue
	add	x8, x8, :lo12:gSysFreeQueue
	strh	w0, [x8]
	add	x0, x8, #8                      // =8
	mov	w2, #2048
	mov	w1, wzr
	mov	x29, sp
	stur	wzr, [x8, #2]
	strh	wzr, [x8, #6]
	bl	memset
	mov	w0, wzr
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end25:
	.size	FtlFreeSysBlkQueueInit, .Lfunc_end25-FtlFreeSysBlkQueueInit
                                        // -- End function
	.globl	FtlLoadBbt                      // -- Begin function FtlLoadBbt
	.p2align	2
	.type	FtlLoadBbt,@function
FtlLoadBbt:                             // @FtlLoadBbt
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-64]!           // 16-byte Folded Spill
	stp	x24, x23, [sp, #16]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #32]             // 16-byte Folded Spill
	adrp	x21, p_sys_data_buf
	adrp	x8, p_sys_spare_buf
	adrp	x24, c_ftl_nand_blks_per_die
	stp	x20, x19, [sp, #48]             // 16-byte Folded Spill
	adrp	x20, gBbtInfo
	ldr	x10, [x21, :lo12:p_sys_data_buf]
	ldr	x22, [x8, :lo12:p_sys_spare_buf]
	ldrh	w8, [x24, :lo12:c_ftl_nand_blks_per_die]
	add	x20, x20, :lo12:gBbtInfo
	mov	w9, #65535
	strh	w9, [x20]
	adrp	x9, req_sys+8
	add	x9, x9, :lo12:req_sys+8
	stp	x10, x22, [x9]
	sub	w9, w8, #1                      // =1
	sub	w8, w8, #16                     // =16
	and	w23, w9, #0xffff
	cmp	w8, w23
	mov	x8, #-1
	mov	x29, sp
	strh	wzr, [x20, #6]
	stur	x8, [x20, #12]
	stur	x8, [x20, #20]
	b.ge	.LBB26_19
// %bb.1:
	adrp	x19, req_sys
	add	x19, x19, :lo12:req_sys
	b	.LBB26_4
.LBB26_2:                               //   in Loop: Header=BB26_4 Depth=1
	ldrsh	w8, [x22]
	cmn	w8, #3887                       // =3887
	b.eq	.LBB26_7
.LBB26_3:                               //   in Loop: Header=BB26_4 Depth=1
	ldrh	w8, [x24, :lo12:c_ftl_nand_blks_per_die]
	sub	w9, w23, #1                     // =1
	and	w23, w9, #0xffff
	sub	w8, w8, #16                     // =16
	cmp	w8, w23
	b.ge	.LBB26_6
.LBB26_4:                               // =>This Inner Loop Header: Depth=1
	lsl	w8, w23, #10
	mov	w1, #1
	mov	x0, x19
	str	w8, [x19, #4]
	bl	FlashReadPages
	ldr	w8, [x19]
	cmn	w8, #1                          // =1
	b.ne	.LBB26_2
// %bb.5:                               //   in Loop: Header=BB26_4 Depth=1
	ldr	w8, [x19, #4]
	mov	w1, #1
	mov	x0, x19
	add	w8, w8, #1                      // =1
	str	w8, [x19, #4]
	bl	FlashReadPages
	ldr	w8, [x19]
	cmn	w8, #1                          // =1
	b.ne	.LBB26_2
	b	.LBB26_3
.LBB26_6:
	adrp	x8, gBbtInfo
	ldrh	w23, [x8, :lo12:gBbtInfo]
	b	.LBB26_8
.LBB26_7:
	strh	w23, [x20]
	ldr	w8, [x22, #4]
	str	w8, [x20, #8]
	ldrh	w8, [x22, #8]
	strh	w8, [x20, #4]
.LBB26_8:
	mvn	w8, w23
	tst	w8, #0xffff
	b.eq	.LBB26_19
// %bb.9:
	adrp	x8, gBbtInfo+4
	ldrh	w8, [x8, :lo12:gBbtInfo+4]
	mov	w9, #65535
	cmp	w8, w9
	b.eq	.LBB26_14
// %bb.10:
	lsl	w8, w8, #10
	mov	w1, #1
	mov	x0, x19
	str	w8, [x19, #4]
	bl	FlashReadPages
	ldr	w8, [x19]
	cmn	w8, #1                          // =1
	b.eq	.LBB26_14
// %bb.11:
	ldrsh	w8, [x22]
	cmn	w8, #3887                       // =3887
	b.ne	.LBB26_14
// %bb.12:
	adrp	x9, gBbtInfo+8
	ldr	w8, [x22, #4]
	ldr	w9, [x9, :lo12:gBbtInfo+8]
	cmp	w8, w9
	b.ls	.LBB26_14
// %bb.13:
	ldrh	w9, [x20, #4]
	str	w8, [x20, #8]
	strh	w9, [x20]
	ldrh	w8, [x22, #8]
	strh	w8, [x20, #4]
.LBB26_14:
	ldrh	w0, [x20]
	mov	w1, #1
	bl	FtlGetLastWrittenPage
	add	w8, w0, #1                      // =1
	strh	w8, [x20, #2]
	tbnz	w0, #15, .LBB26_20
// %bb.15:
	and	w23, w0, #0xffff
	adrp	x24, gBbtInfo
	b	.LBB26_17
.LBB26_16:                              //   in Loop: Header=BB26_17 Depth=1
	cmp	w23, #0                         // =0
	sub	w23, w23, #1                    // =1
	b.le	.LBB26_20
.LBB26_17:                              // =>This Inner Loop Header: Depth=1
	ldrh	w8, [x24, :lo12:gBbtInfo]
	ldr	x9, [x21, :lo12:p_sys_data_buf]
	mov	w1, #1
	mov	x0, x19
	orr	w8, w23, w8, lsl #10
	str	w8, [x19, #4]
	str	x9, [x19, #8]
	bl	FlashReadPages
	ldr	w8, [x19]
	cmn	w8, #1                          // =1
	b.eq	.LBB26_16
// %bb.18:                              //   in Loop: Header=BB26_17 Depth=1
	ldrsh	w8, [x22]
	cmn	w8, #3887                       // =3887
	b.ne	.LBB26_16
	b	.LBB26_21
.LBB26_19:
	mov	w0, #-1
	b	.LBB26_30
.LBB26_20:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlLoadBbt
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlLoadBbt
	mov	w2, #251
	bl	sftl_printk
.LBB26_21:
	ldrh	w8, [x22, #10]
	adrp	x9, gBbtInfo+6
	strh	w8, [x9, :lo12:gBbtInfo+6]
	ldrh	w9, [x22, #12]
	mov	w8, #65535
	cmp	w9, w8
	b.eq	.LBB26_26
// %bb.22:
	adrp	x8, c_ftl_nand_sys_blks_per_plane
	ldr	w11, [x8, :lo12:c_ftl_nand_sys_blks_per_plane]
	cmp	w11, w9
	b.eq	.LBB26_26
// %bb.23:
	adrp	x10, c_ftl_nand_blk_pre_plane
	ldrh	w10, [x10, :lo12:c_ftl_nand_blk_pre_plane]
	lsr	w12, w10, #2
	cmp	w9, w12
	b.hs	.LBB26_26
// %bb.24:
	cmp	w11, w12
	b.hs	.LBB26_26
// %bb.25:
	cmp	w9, #24                         // =24
	mov	w11, #24
	csel	w9, w9, w11, hi
	adrp	x11, c_ftl_nand_data_blks_per_plane
	sub	w10, w10, w9
	strh	w10, [x11, :lo12:c_ftl_nand_data_blks_per_plane]
	adrp	x10, c_ftl_nand_planes_num
	adrp	x11, c_ftl_nand_totle_phy_blks
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_num]
	ldr	w11, [x11, :lo12:c_ftl_nand_totle_phy_blks]
	str	w9, [x8, :lo12:c_ftl_nand_sys_blks_per_plane]
	adrp	x8, c_ftl_nand_max_sys_blks
	mul	w9, w10, w9
	str	w9, [x8, :lo12:c_ftl_nand_max_sys_blks]
	sub	w8, w11, w9
	adrp	x9, c_ftl_nand_max_data_blks
	str	w8, [x9, :lo12:c_ftl_nand_max_data_blks]
.LBB26_26:
	adrp	x19, c_ftl_nand_die_num
	ldrh	w8, [x19, :lo12:c_ftl_nand_die_num]
	cbz	w8, .LBB26_29
// %bb.27:
	mov	x21, xzr
	adrp	x22, req_sys+8
	adrp	x23, c_ftl_nand_bbm_buf_size
.LBB26_28:                              // =>This Inner Loop Header: Depth=1
	ldrh	w8, [x23, :lo12:c_ftl_nand_bbm_buf_size]
	ldr	x9, [x22, :lo12:req_sys+8]
	add	x10, x20, x21, lsl #3
	ldr	x0, [x10, #32]
	mul	w10, w21, w8
	add	x1, x9, w10, uxtw #2
	lsl	x2, x8, #2
	bl	memcpy
	ldrh	w8, [x19, :lo12:c_ftl_nand_die_num]
	add	x21, x21, #1                    // =1
	cmp	x21, x8
	b.lo	.LBB26_28
.LBB26_29:
	mov	w0, wzr
.LBB26_30:
	ldp	x20, x19, [sp, #48]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #32]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #16]             // 16-byte Folded Reload
	ldp	x29, x30, [sp], #64             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end26:
	.size	FtlLoadBbt, .Lfunc_end26-FtlLoadBbt
                                        // -- End function
	.globl	FtlMakeBbt                      // -- Begin function FtlMakeBbt
	.p2align	2
	.type	FtlMakeBbt,@function
FtlMakeBbt:                             // @FtlMakeBbt
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-96]!           // 16-byte Folded Spill
	stp	x26, x25, [sp, #32]             // 16-byte Folded Spill
	adrp	x25, gBbtInfo
	stp	x28, x27, [sp, #16]             // 16-byte Folded Spill
	add	x25, x25, :lo12:gBbtInfo
	mov	x8, #-1
	adrp	x28, p_sys_data_buf
	adrp	x9, p_sys_spare_buf
	adrp	x27, c_ftl_nand_die_num
	stp	x22, x21, [sp, #64]             // 16-byte Folded Spill
	stur	x8, [x25, #12]
	stur	x8, [x25, #20]
	ldr	x8, [x28, :lo12:p_sys_data_buf]
	ldr	x21, [x9, :lo12:p_sys_spare_buf]
	ldrh	w9, [x27, :lo12:c_ftl_nand_die_num]
	adrp	x10, req_sys+8
	stp	x20, x19, [sp, #80]             // 16-byte Folded Spill
	mov	w20, #65535
	add	x10, x10, :lo12:req_sys+8
	adrp	x26, c_ftl_nand_blks_per_die
	stp	x24, x23, [sp, #48]             // 16-byte Folded Spill
	mov	x29, sp
	strh	w20, [x25]
	strh	wzr, [x25, #6]
	stp	x8, x21, [x10]
	cbz	w9, .LBB27_34
// %bb.1:
	adrp	x19, req_sys
	mov	x22, xzr
	add	x19, x19, :lo12:req_sys
	b	.LBB27_3
.LBB27_2:                               //   in Loop: Header=BB27_3 Depth=1
	ldrh	w8, [x27, :lo12:c_ftl_nand_die_num]
	add	x22, x22, #1                    // =1
	cmp	x22, x8
	b.hs	.LBB27_8
.LBB27_3:                               // =>This Loop Header: Depth=1
                                        //     Child Loop BB27_5 Depth 2
	ldrh	w8, [x26, :lo12:c_ftl_nand_blks_per_die]
	add	x23, x25, x22, lsl #1
	strh	w20, [x23, #12]!
	sub	w9, w8, #1                      // =1
	and	w24, w9, #0xffff
	sub	w9, w8, #16                     // =16
	cmp	w9, w24
	b.lt	.LBB27_5
	b	.LBB27_2
.LBB27_4:                               //   in Loop: Header=BB27_5 Depth=2
	ldrh	w8, [x26, :lo12:c_ftl_nand_blks_per_die]
	sub	w9, w24, #1                     // =1
	and	w24, w9, #0xffff
	sub	w9, w8, #16                     // =16
	cmp	w9, w24
	b.ge	.LBB27_2
.LBB27_5:                               //   Parent Loop BB27_3 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	madd	w8, w8, w22, w24
	lsl	w8, w8, #10
	mov	w1, #1
	mov	x0, x19
	str	w8, [x19, #4]
	bl	FlashReadPages
	ldr	w8, [x19]
	cmn	w8, #1                          // =1
	b.eq	.LBB27_4
// %bb.6:                               //   in Loop: Header=BB27_5 Depth=2
	ldrsh	w8, [x21]
	cmn	w8, #3872                       // =3872
	b.ne	.LBB27_4
// %bb.7:                               //   in Loop: Header=BB27_3 Depth=1
	strh	w24, [x23]
	b	.LBB27_2
.LBB27_8:
	cbz	w8, .LBB27_34
// %bb.9:
	mov	x20, xzr
	b	.LBB27_12
.LBB27_10:                              //   in Loop: Header=BB27_12 Depth=1
	ldrh	w9, [x26, :lo12:c_ftl_nand_blks_per_die]
	mov	w1, #1
	mov	x0, x19
	madd	w23, w20, w9, w8
	lsl	w8, w23, #10
	str	w8, [x19, #4]
	bl	FlashReadPages
	ldrh	w9, [x26, :lo12:c_ftl_nand_blks_per_die]
	add	x8, x25, x20, lsl #3
	ldr	x0, [x8, #32]
	ldr	x1, [x19, #8]
	add	x8, x9, #7                      // =7
	lsr	x2, x8, #3
	bl	memcpy
.LBB27_11:                              //   in Loop: Header=BB27_12 Depth=1
	ldrh	w8, [x26, :lo12:c_ftl_nand_blks_per_die]
	and	w1, w23, #0xffff
	mov	w12, #1
	adrp	x0, .L.str.82
	udiv	w2, w1, w8
	add	x9, x25, w2, uxtw #3
	ldr	x9, [x9, #32]
	msub	w8, w2, w8, w23
	ubfx	w10, w8, #3, #13
	and	x10, x10, #0x1ffc
	ldr	w11, [x9, x10]
	and	w3, w8, #0xffff
	lsl	w8, w12, w8
	add	x0, x0, :lo12:.L.str.82
	orr	w4, w8, w11
	adrp	x21, c_ftl_nand_die_num
	str	w4, [x9, x10]
	bl	sftl_printk
	ldrh	w8, [x25, #6]
	ldrh	w9, [x21, :lo12:c_ftl_nand_die_num]
	add	x20, x20, #1                    // =1
	add	w8, w8, #1                      // =1
	cmp	x20, x9
	strh	w8, [x25, #6]
	b.hs	.LBB27_34
.LBB27_12:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB27_20 Depth 2
                                        //     Child Loop BB27_23 Depth 2
                                        //     Child Loop BB27_26 Depth 2
                                        //     Child Loop BB27_30 Depth 2
                                        //       Child Loop BB27_31 Depth 3
	adrp	x8, p_sys_spare_buf
	ldr	x24, [x28, :lo12:p_sys_data_buf]
	ldr	x27, [x8, :lo12:p_sys_spare_buf]
	adrp	x8, req_sys+8
	add	x8, x8, :lo12:req_sys+8
	add	x22, x25, x20, lsl #1
	stp	x24, x27, [x8]
	ldrh	w8, [x22, #12]!
	mov	w9, #65535
	cmp	w8, w9
	b.ne	.LBB27_10
// %bb.13:                              //   in Loop: Header=BB27_12 Depth=1
	mov	x21, #-1
	stp	x21, x21, [x24, #240]
	stp	x21, x21, [x24, #224]
	stp	x21, x21, [x24, #208]
	stp	x21, x21, [x24, #192]
	stp	x21, x21, [x24, #176]
	stp	x21, x21, [x24, #160]
	stp	x21, x21, [x24, #144]
	stp	x21, x21, [x24, #128]
	stp	x21, x21, [x24, #112]
	stp	x21, x21, [x24, #96]
	stp	x21, x21, [x24, #80]
	stp	x21, x21, [x24, #64]
	stp	x21, x21, [x24, #48]
	stp	x21, x21, [x24, #32]
	stp	x21, x21, [x24, #16]
	stp	x21, x21, [x24]
	adrp	x8, g_nand_ops
	ldr	x8, [x8, :lo12:g_nand_ops]
	mov	x0, x24
	mov	w1, w20
	blr	x8
	and	w8, w0, #0xffff
	cmp	w8, #51                         // =51
	b.lo	.LBB27_15
// %bb.14:                              //   in Loop: Header=BB27_12 Depth=1
	stp	x21, x21, [x24, #240]
	stp	x21, x21, [x24, #224]
	stp	x21, x21, [x24, #208]
	stp	x21, x21, [x24, #192]
	stp	x21, x21, [x24, #176]
	stp	x21, x21, [x24, #160]
	stp	x21, x21, [x24, #144]
	stp	x21, x21, [x24, #128]
	stp	x21, x21, [x24, #112]
	stp	x21, x21, [x24, #96]
	stp	x21, x21, [x24, #80]
	stp	x21, x21, [x24, #64]
	stp	x21, x21, [x24, #48]
	stp	x21, x21, [x24, #32]
	stp	x21, x21, [x24, #16]
	stp	x21, x21, [x24]
	b	.LBB27_24
.LBB27_15:                              //   in Loop: Header=BB27_12 Depth=1
	tst	w0, #0xffff
	b.eq	.LBB27_24
// %bb.16:                              //   in Loop: Header=BB27_12 Depth=1
	adrp	x9, g_nand_phy_info+14
	ldrh	w9, [x9, :lo12:g_nand_phy_info+14]
	cmp	w9, #4                          // =4
	b.ne	.LBB27_24
// %bb.17:                              //   in Loop: Header=BB27_12 Depth=1
	cmp	w8, #2                          // =2
	mov	w8, w8
	b.hs	.LBB27_19
// %bb.18:                              //   in Loop: Header=BB27_12 Depth=1
	mov	x9, xzr
	b	.LBB27_22
.LBB27_19:                              //   in Loop: Header=BB27_12 Depth=1
	and	x9, x8, #0xfffe
	add	x10, x24, #2                    // =2
	mov	x11, x9
.LBB27_20:                              //   Parent Loop BB27_12 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldurh	w12, [x10, #-2]
	ldrh	w13, [x10]
	subs	x11, x11, #2                    // =2
	lsr	w12, w12, #1
	lsr	w13, w13, #1
	sturh	w12, [x10, #-2]
	strh	w13, [x10], #4
	b.ne	.LBB27_20
// %bb.21:                              //   in Loop: Header=BB27_12 Depth=1
	cmp	x9, x8
	b.eq	.LBB27_24
.LBB27_22:                              //   in Loop: Header=BB27_12 Depth=1
	sub	x8, x8, x9
	add	x9, x24, x9, lsl #1
.LBB27_23:                              //   Parent Loop BB27_12 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldrh	w10, [x9]
	subs	x8, x8, #1                      // =1
	lsr	w10, w10, #1
	strh	w10, [x9], #2
	b.ne	.LBB27_23
.LBB27_24:                              //   in Loop: Header=BB27_12 Depth=1
	adrp	x8, req_sys+8
	ldr	x23, [x8, :lo12:req_sys+8]
	add	x21, x25, x20, lsl #3
	adrp	x8, c_ftl_nand_bbm_buf_size
	ldr	x24, [x21, #32]!
	ldrh	w8, [x8, :lo12:c_ftl_nand_bbm_buf_size]
	mov	w1, wzr
	mov	x0, x24
	lsl	x2, x8, #2
	bl	memset
	mov	x28, xzr
	b	.LBB27_26
.LBB27_25:                              //   in Loop: Header=BB27_26 Depth=2
	lsr	w9, w8, #3
	and	x9, x9, #0x1ffc
	ldr	w10, [x24, x9]
	mov	w11, #1
	lsl	w8, w11, w8
	add	x28, x28, #2                    // =2
	orr	w8, w8, w10
	cmp	x28, #1024                      // =1024
	str	w8, [x24, x9]
	b.eq	.LBB27_29
.LBB27_26:                              //   Parent Loop BB27_12 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldrh	w8, [x23, x28]
	mov	w9, #65535
	cmp	w8, w9
	b.eq	.LBB27_29
// %bb.27:                              //   in Loop: Header=BB27_26 Depth=2
	ldrh	w9, [x26, :lo12:c_ftl_nand_blks_per_die]
	cmp	w8, w9
	b.lo	.LBB27_25
// %bb.28:                              //   in Loop: Header=BB27_26 Depth=2
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlBbt2Bitmap
	mov	w2, #74
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlBbt2Bitmap
	bl	sftl_printk
	ldrh	w8, [x23, x28]
	b	.LBB27_25
.LBB27_29:                              //   in Loop: Header=BB27_12 Depth=1
	ldrh	w8, [x26, :lo12:c_ftl_nand_blks_per_die]
	adrp	x28, p_sys_data_buf
	sub	w24, w8, #1                     // =1
.LBB27_30:                              //   Parent Loop BB27_12 Depth=1
                                        // =>  This Loop Header: Depth=2
                                        //       Child Loop BB27_31 Depth 3
	mul	w9, w8, w20
	mov	w10, w24
.LBB27_31:                              //   Parent Loop BB27_12 Depth=1
                                        //     Parent Loop BB27_30 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	mov	w24, w10
	add	w10, w9, w10
	and	w11, w10, #0xffff
	and	w12, w8, #0xffff
	udiv	w11, w11, w12
	add	x12, x25, w11, uxtw #3
	ldr	x12, [x12, #32]
	msub	w10, w11, w8, w10
	lsr	w11, w10, #3
	and	x11, x11, #0x1ffc
	ldr	w11, [x12, x11]
	lsr	w11, w11, w10
	sub	w10, w24, #1                    // =1
	tbnz	w11, #0, .LBB27_31
// %bb.32:                              //   in Loop: Header=BB27_30 Depth=2
	adrp	x8, p_sys_spare_buf
	ldr	x8, [x8, :lo12:p_sys_spare_buf]
	strh	w24, [x22]
	mov	w2, #4096
	mov	w1, wzr
	stp	xzr, xzr, [x8]
	ldr	x0, [x28, :lo12:p_sys_data_buf]
	bl	memset
	mov	w8, #61664
	strh	w8, [x27]
	str	wzr, [x27, #4]
	ldrh	w8, [x22]
	adrp	x10, c_ftl_nand_bbm_buf_size
	strh	w8, [x27, #2]
	ldrh	w8, [x26, :lo12:c_ftl_nand_blks_per_die]
	ldrh	w9, [x22]
	ldrh	w10, [x10, :lo12:c_ftl_nand_bbm_buf_size]
	ldr	x0, [x19, #8]
	ldr	x1, [x21]
	madd	w23, w20, w8, w9
	lsl	w8, w23, #10
	lsl	x2, x10, #2
	str	w8, [x19, #4]
	bl	memcpy
	mov	w2, #1
	mov	x0, x19
	bl	FlashEraseBlocks
	mov	w1, #1
	mov	w2, #1
	mov	x0, x19
	bl	FlashProgPages
	ldr	w8, [x19]
	cmn	w8, #1                          // =1
	b.ne	.LBB27_11
// %bb.33:                              //   in Loop: Header=BB27_30 Depth=2
	ldrh	w8, [x26, :lo12:c_ftl_nand_blks_per_die]
	and	w1, w23, #0xffff
	mov	w12, #1
	adrp	x0, .L.str.82
	udiv	w2, w1, w8
	add	x9, x25, w2, uxtw #3
	ldr	x9, [x9, #32]
	msub	w8, w2, w8, w23
	ubfx	w10, w8, #3, #13
	and	x10, x10, #0x1ffc
	ldr	w11, [x9, x10]
	and	w3, w8, #0xffff
	lsl	w8, w12, w8
	add	x0, x0, :lo12:.L.str.82
	orr	w4, w8, w11
	str	w4, [x9, x10]
	bl	sftl_printk
	ldrh	w9, [x25, #6]
	ldrh	w8, [x26, :lo12:c_ftl_nand_blks_per_die]
	add	w9, w9, #1                      // =1
	strh	w9, [x25, #6]
	b	.LBB27_30
.LBB27_34:
	adrp	x21, c_ftl_nand_reserved_blks
	ldrh	w8, [x21, :lo12:c_ftl_nand_reserved_blks]
	cbz	w8, .LBB27_37
// %bb.35:
	adrp	x20, .L.str.82
	mov	w19, wzr
	mov	w22, #1
	add	x20, x20, :lo12:.L.str.82
.LBB27_36:                              // =>This Inner Loop Header: Depth=1
	ldrh	w8, [x26, :lo12:c_ftl_nand_blks_per_die]
	and	w9, w19, #0xffff
	mov	x0, x20
	mov	w1, w19
	udiv	w2, w9, w8
	add	x9, x25, w2, uxtw #3
	mul	w8, w2, w8
	ldr	x9, [x9, #32]
	sub	w3, w19, w8, uxth
	lsr	w8, w3, #5
	lsl	x8, x8, #2
	ldr	w10, [x9, x8]
	lsl	w11, w22, w3
	orr	w4, w11, w10
	str	w4, [x9, x8]
	bl	sftl_printk
	ldrh	w8, [x25, #6]
	ldrh	w9, [x21, :lo12:c_ftl_nand_reserved_blks]
	add	w19, w19, #1                    // =1
	add	w8, w8, #1                      // =1
	cmp	w19, w9
	strh	w8, [x25, #6]
	b.lo	.LBB27_36
.LBB27_37:
	adrp	x22, gBbtInfo+12
	ldrh	w8, [x22, :lo12:gBbtInfo+12]
	sub	w9, w8, #1                      // =1
	and	w21, w9, #0xffff
	sub	w8, w8, #48                     // =48
	cmp	w8, w21
	b.ge	.LBB27_46
// %bb.38:
	adrp	x20, .L.str.82
	adrp	x23, gBbtInfo
	mov	w24, #1
	add	x20, x20, :lo12:.L.str.82
	mov	w19, w21
	b	.LBB27_41
.LBB27_39:                              //   in Loop: Header=BB27_41 Depth=1
	ldrh	w8, [x26, :lo12:c_ftl_nand_blks_per_die]
	and	w9, w19, #0xffff
	mov	x0, x20
	mov	w1, w21
	udiv	w2, w9, w8
	add	x9, x25, w2, uxtw #3
	ldr	x9, [x9, #32]
	msub	w8, w2, w8, w19
	ubfx	w10, w8, #3, #13
	and	x10, x10, #0x1ffc
	ldr	w11, [x9, x10]
	and	w3, w8, #0xffff
	lsl	w8, w24, w8
	orr	w4, w8, w11
	str	w4, [x9, x10]
	bl	sftl_printk
	ldrh	w8, [x25, #6]
	add	w8, w8, #1                      // =1
	strh	w8, [x25, #6]
.LBB27_40:                              //   in Loop: Header=BB27_41 Depth=1
	ldrh	w8, [x22, :lo12:gBbtInfo+12]
	sub	w19, w19, #1                    // =1
	and	w21, w19, #0xffff
	sub	w8, w8, #48                     // =48
	cmp	w8, w21
	b.ge	.LBB27_46
.LBB27_41:                              // =>This Inner Loop Header: Depth=1
	ldrh	w8, [x26, :lo12:c_ftl_nand_blks_per_die]
	and	w10, w19, #0xffff
	and	w9, w8, #0xffff
	udiv	w9, w10, w9
	add	x10, x25, w9, uxtw #3
	ldr	x10, [x10, #32]
	msub	w8, w9, w8, w19
	lsr	w9, w8, #3
	and	x9, x9, #0x1ffc
	ldr	w9, [x10, x9]
	lsr	w8, w9, w8
	tbnz	w8, #0, .LBB27_40
// %bb.42:                              //   in Loop: Header=BB27_41 Depth=1
	mov	w0, w19
	bl	FlashTestBlk
	cbnz	w0, .LBB27_39
// %bb.43:                              //   in Loop: Header=BB27_41 Depth=1
	ldrsh	w8, [x23, :lo12:gBbtInfo]
	cmn	w8, #1                          // =1
	b.ne	.LBB27_45
// %bb.44:                              //   in Loop: Header=BB27_41 Depth=1
	strh	w19, [x23, :lo12:gBbtInfo]
	b	.LBB27_40
.LBB27_45:
	adrp	x8, gBbtInfo+4
	strh	w19, [x8, :lo12:gBbtInfo+4]
.LBB27_46:
	ldrh	w8, [x25]
	adrp	x9, req_erase
	ldr	x10, [x9, :lo12:req_erase]
	strh	wzr, [x25, #2]
	lsl	w8, w8, #10
	str	wzr, [x25, #8]
	str	w8, [x10, #4]
	ldrh	w8, [x25, #4]
	ldr	x10, [x9, :lo12:req_erase]
	mov	w2, #2
	lsl	w8, w8, #10
	str	w8, [x10, #36]
	ldr	x0, [x9, :lo12:req_erase]
	bl	FlashEraseBlocks
	ldrh	w1, [x25]
	ldrh	w8, [x26, :lo12:c_ftl_nand_blks_per_die]
	add	x20, x25, #32                   // =32
	mov	w21, #1
	adrp	x19, .L.str.82
	udiv	w2, w1, w8
	ldr	x9, [x20, w2, uxtw #3]
	msub	w8, w2, w8, w1
	ubfx	w10, w8, #3, #13
	and	x10, x10, #0x1ffc
	ldr	w11, [x9, x10]
	and	w3, w8, #0xffff
	lsl	w8, w21, w8
	add	x19, x19, :lo12:.L.str.82
	orr	w4, w8, w11
	mov	x0, x19
	str	w4, [x9, x10]
	bl	sftl_printk
	ldrh	w8, [x25, #6]
	ldrh	w1, [x25, #4]
	ldrh	w9, [x26, :lo12:c_ftl_nand_blks_per_die]
	mov	x0, x19
	add	w8, w8, #1                      // =1
	strh	w8, [x25, #6]
	udiv	w2, w1, w9
	ldr	x8, [x20, w2, uxtw #3]
	msub	w9, w2, w9, w1
	ubfx	w10, w9, #3, #13
	and	x10, x10, #0x1ffc
	ldr	w11, [x8, x10]
	and	w3, w9, #0xffff
	lsl	w9, w21, w9
	orr	w4, w9, w11
	str	w4, [x8, x10]
	bl	sftl_printk
	ldrh	w8, [x25, #6]
	add	w8, w8, #1                      // =1
	strh	w8, [x25, #6]
	bl	FtlBbmTblFlush
	ldr	w8, [x25, #8]
	ldrh	w9, [x25, #4]
	ldrh	w10, [x25]
	strh	wzr, [x25, #2]
	add	w8, w8, #1                      // =1
	strh	w9, [x25]
	str	w8, [x25, #8]
	strh	w10, [x25, #4]
	bl	FtlBbmTblFlush
	ldp	x20, x19, [sp, #80]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #64]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #48]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #32]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #96             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end27:
	.size	FtlMakeBbt, .Lfunc_end27-FtlMakeBbt
                                        // -- End function
	.globl	FtlBbmTblFlush                  // -- Begin function FtlBbmTblFlush
	.p2align	2
	.type	FtlBbmTblFlush,@function
FtlBbmTblFlush:                         // @FtlBbmTblFlush
// %bb.0:
	hint	#25
	sub	sp, sp, #112                    // =112
	adrp	x9, p_sys_data_buf
	adrp	x10, p_sys_spare_buf
	adrp	x8, c_ftl_nand_byte_pre_page
	ldr	x0, [x9, :lo12:p_sys_data_buf]
	ldr	x9, [x10, :lo12:p_sys_spare_buf]
	ldrh	w2, [x8, :lo12:c_ftl_nand_byte_pre_page]
	adrp	x8, req_sys+8
	add	x8, x8, :lo12:req_sys+8
	mov	w1, wzr
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	stp	x28, x27, [sp, #32]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #48]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #64]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #80]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #96]             // 16-byte Folded Spill
	add	x29, sp, #16                    // =16
	stp	x0, x9, [x8]
	bl	memset
	adrp	x19, c_ftl_nand_die_num
	ldrh	w8, [x19, :lo12:c_ftl_nand_die_num]
	adrp	x24, gBbtInfo
	add	x24, x24, :lo12:gBbtInfo
	cbz	w8, .LBB28_3
// %bb.1:
	mov	x20, xzr
	adrp	x21, req_sys+8
	adrp	x22, c_ftl_nand_bbm_buf_size
.LBB28_2:                               // =>This Inner Loop Header: Depth=1
	ldrh	w8, [x22, :lo12:c_ftl_nand_bbm_buf_size]
	ldr	x9, [x21, :lo12:req_sys+8]
	add	x10, x24, x20, lsl #3
	ldr	x1, [x10, #32]
	mul	w10, w20, w8
	add	x0, x9, w10, sxtw #2
	lsl	x2, x8, #2
	bl	memcpy
	ldrh	w8, [x19, :lo12:c_ftl_nand_die_num]
	add	x20, x20, #1                    // =1
	cmp	x20, x8
	b.lo	.LBB28_2
.LBB28_3:
	adrp	x8, req_sys+16
	ldr	x27, [x8, :lo12:req_sys+16]
	mov	x8, #-1
	mov	w9, #61649
	adrp	x10, g_totle_avg_erase_count
	str	x8, [x27, #8]
	stur	x8, [x27, #2]
	strh	w9, [x27]
	ldr	w8, [x24, #8]
	adrp	x9, c_ftl_nand_sys_blks_per_plane
	adrp	x19, req_sys
	adrp	x20, .L.str.83
	str	w8, [x27, #4]
	ldrh	w8, [x24]
	adrp	x21, .L.str.84
	mov	w12, wzr
	mov	w11, wzr
	strh	w8, [x27, #2]
	ldrh	w8, [x24, #4]
	add	x19, x19, :lo12:req_sys
	add	x20, x20, :lo12:.L.str.83
	adrp	x26, c_ftl_nand_page_pre_slc_blk
	strh	w8, [x27, #8]
	ldrh	w8, [x24, #6]
	adrp	x25, gBbtInfo+2
	adrp	x28, req_sys+4
	add	x21, x21, :lo12:.L.str.84
	strh	w8, [x27, #10]
	ldr	w8, [x9, :lo12:c_ftl_nand_sys_blks_per_plane]
	adrp	x22, req_erase
	strh	w8, [x27, #12]
	ldr	w8, [x10, :lo12:g_totle_avg_erase_count]
	strh	w8, [x27, #14]
.LBB28_4:                               // =>This Loop Header: Depth=1
                                        //     Child Loop BB28_5 Depth 2
	mov	w23, w11
	stur	w12, [x29, #-4]                 // 4-byte Folded Spill
	str	w11, [sp, #8]                   // 4-byte Folded Spill
.LBB28_5:                               //   Parent Loop BB28_4 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	adrp	x8, p_sys_data_buf
	adrp	x9, p_sys_spare_buf
	ldr	x8, [x8, :lo12:p_sys_data_buf]
	ldr	x9, [x9, :lo12:p_sys_spare_buf]
	ldrh	w1, [x24]
	ldrh	w2, [x24, #2]
	ldrh	w3, [x24, #4]
	stp	x8, x9, [x19, #8]
	mov	x0, x20
	orr	w8, w2, w1, lsl #10
	stp	wzr, w8, [x19]
	ldrh	w4, [x27, #10]
	bl	sftl_printk
	ldrh	w8, [x26, :lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w9, [x24, #2]
	sub	w8, w8, #1                      // =1
	cmp	w8, w9
	b.gt	.LBB28_7
// %bb.6:                               //   in Loop: Header=BB28_5 Depth=2
	ldr	w8, [x24, #8]
	mov	w2, #1
	add	w8, w8, #1                      // =1
	str	w8, [x24, #8]
	str	w8, [x27, #4]
	ldrh	w8, [x24]
	strh	w8, [x27, #8]
	ldrh	w9, [x24, #4]
	strh	w8, [x24, #4]
	ldr	x8, [x22, :lo12:req_erase]
	strh	wzr, [x24, #2]
	strh	w9, [x24]
	lsl	w9, w9, #10
	str	w9, [x28, :lo12:req_sys+4]
	str	w9, [x8, #4]
	ldr	x0, [x22, :lo12:req_erase]
	bl	FlashEraseBlocks
.LBB28_7:                               //   in Loop: Header=BB28_5 Depth=2
	mov	w1, #1
	mov	w2, #1
	mov	x0, x19
	bl	FlashProgPages
	ldrh	w9, [x25, :lo12:gBbtInfo+2]
	ldr	w8, [x19]
	add	w9, w9, #1                      // =1
	cmn	w8, #1                          // =1
	strh	w9, [x25, :lo12:gBbtInfo+2]
	b.ne	.LBB28_9
// %bb.8:                               //   in Loop: Header=BB28_5 Depth=2
	ldr	w1, [x28, :lo12:req_sys+4]
	mov	x0, x21
	bl	sftl_printk
	add	w23, w23, #1                    // =1
	and	w8, w23, #0xffff
	cmp	w8, #4                          // =4
	b.lo	.LBB28_5
	b	.LBB28_12
.LBB28_9:                               //   in Loop: Header=BB28_4 Depth=1
	ldur	w12, [x29, #-4]                 // 4-byte Folded Reload
	mov	w11, w23
	add	w12, w12, #1                    // =1
	cmp	w12, #2                         // =2
	b.lt	.LBB28_4
// %bb.10:                              //   in Loop: Header=BB28_4 Depth=1
	cmp	w8, #256                        // =256
	mov	w11, w23
	b.eq	.LBB28_4
// %bb.11:
	ldp	x20, x19, [sp, #96]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #80]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #64]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #48]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	add	sp, sp, #112                    // =112
	hint	#29
	ret
.LBB28_12:
	ldr	w9, [sp, #8]                    // 4-byte Folded Reload
	ldr	w1, [x28, :lo12:req_sys+4]
	adrp	x0, .L.str.85
	add	x0, x0, :lo12:.L.str.85
	add	w8, w9, #1                      // =1
	and	w8, w8, #0xffff
	cmp	w8, #4                          // =4
	mov	w8, #4
	csinc	w8, w8, w9, ls
	and	w2, w8, #0xffff
	bl	sftl_printk
.LBB28_13:                              // =>This Inner Loop Header: Depth=1
	b	.LBB28_13
.Lfunc_end28:
	.size	FtlBbmTblFlush, .Lfunc_end28-FtlBbmTblFlush
                                        // -- End function
	.globl	make_superblock                 // -- Begin function make_superblock
	.p2align	2
	.type	make_superblock,@function
make_superblock:                        // @make_superblock
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	adrp	x9, c_ftl_nand_data_blks_per_plane
	ldrh	w8, [x0]
	ldrh	w9, [x9, :lo12:c_ftl_nand_data_blks_per_plane]
	str	x19, [sp, #16]                  // 8-byte Folded Spill
	mov	x19, x0
	mov	x29, sp
	cmp	w8, w9
	b.lo	.LBB29_2
// %bb.1:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.make_superblock
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.make_superblock
	mov	w2, #2157
	bl	sftl_printk
.LBB29_2:
	strh	wzr, [x19, #4]
	strb	wzr, [x19, #7]
	adrp	x10, c_ftl_nand_planes_num
	ldrh	w9, [x10, :lo12:c_ftl_nand_planes_num]
	ldrh	w8, [x19]
	cbz	w9, .LBB29_7
// %bb.3:
	adrp	x13, p_plane_order_table
	adrp	x17, gBbtInfo
	mov	w9, wzr
	mov	x11, xzr
	add	x12, x19, #16                   // =16
	add	x13, x13, :lo12:p_plane_order_table
	adrp	x14, c_ftl_nand_planes_per_die
	adrp	x15, c_ftl_nand_blks_per_die
	mov	w16, #65535
	add	x17, x17, :lo12:gBbtInfo
	b	.LBB29_5
.LBB29_4:                               //   in Loop: Header=BB29_5 Depth=1
	ldrh	w18, [x10, :lo12:c_ftl_nand_planes_num]
	add	x11, x11, #1                    // =1
	cmp	x11, x18
	b.hs	.LBB29_7
.LBB29_5:                               // =>This Inner Loop Header: Depth=1
	ldrb	w18, [x13, x11]
	ldrh	w0, [x14, :lo12:c_ftl_nand_planes_per_die]
	ldrh	w1, [x15, :lo12:c_ftl_nand_blks_per_die]
	strh	w16, [x12, x11, lsl #1]
	and	w2, w18, #0xffff
	and	w3, w0, #0xffff
	udiv	w2, w2, w3
	mul	w3, w0, w8
	madd	w1, w1, w2, w3
	ldrh	w3, [x15, :lo12:c_ftl_nand_blks_per_die]
	msub	w18, w2, w0, w18
	add	w18, w1, w18
	and	w0, w18, #0xffff
	and	w1, w3, #0xffff
	udiv	w0, w0, w1
	add	x1, x17, w0, uxtw #3
	ldr	x1, [x1, #32]
	msub	w0, w0, w3, w18
	lsr	w2, w0, #3
	and	x2, x2, #0x1ffc
	ldr	w1, [x1, x2]
	lsr	w0, w1, w0
	tbnz	w0, #0, .LBB29_4
// %bb.6:                               //   in Loop: Header=BB29_5 Depth=1
	add	w9, w9, #1                      // =1
	strh	w18, [x12, x11, lsl #1]
	strb	w9, [x19, #7]
	b	.LBB29_4
.LBB29_7:
	adrp	x10, c_ftl_nand_page_pre_blk
	ldrh	w10, [x10, :lo12:c_ftl_nand_page_pre_blk]
	and	w9, w9, #0xff
	strb	wzr, [x19, #9]
	mov	w0, wzr
	mul	w9, w10, w9
	strh	w9, [x19, #4]
	adrp	x9, p_erase_count_table
	ldr	x9, [x9, :lo12:p_erase_count_table]
	ldrh	w8, [x9, x8, lsl #1]
	mov	w9, #10000
	cmp	w8, w9
	cset	w8, hi
	strb	w8, [x19, #9]
	ldr	x19, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end29:
	.size	make_superblock, .Lfunc_end29-make_superblock
                                        // -- End function
	.globl	FtlFreeSysBlkQueueOut           // -- Begin function FtlFreeSysBlkQueueOut
	.p2align	2
	.type	FtlFreeSysBlkQueueOut,@function
FtlFreeSysBlkQueueOut:                  // @FtlFreeSysBlkQueueOut
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-48]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #32]             // 16-byte Folded Spill
	adrp	x20, gSysFreeQueue+6
	ldrh	w8, [x20, :lo12:gSysFreeQueue+6]
	str	x21, [sp, #16]                  // 8-byte Folded Spill
	mov	w21, #65535
	mov	x29, sp
	cbz	w8, .LBB30_4
// %bb.1:
	adrp	x9, gSysFreeQueue
	add	x9, x9, :lo12:gSysFreeQueue
	ldrh	w10, [x9, #2]
	sub	w8, w8, #1                      // =1
	mov	w2, #1
	add	x11, x9, x10, lsl #1
	ldrh	w19, [x11, #8]
	strh	w8, [x9, #6]
	adrp	x8, req_erase
	ldr	x11, [x8, :lo12:req_erase]
	add	w10, w10, #1                    // =1
	and	w10, w10, #0x3ff
	strh	w10, [x9, #2]
	lsl	w9, w19, #10
	str	w9, [x11, #4]
	ldr	x0, [x8, :lo12:req_erase]
	bl	FlashEraseBlocks
	adrp	x8, g_totle_sys_slc_erase_count
	ldr	w9, [x8, :lo12:g_totle_sys_slc_erase_count]
	cmp	w19, w21
	add	w9, w9, #1                      // =1
	str	w9, [x8, :lo12:g_totle_sys_slc_erase_count]
	b.eq	.LBB30_5
// %bb.2:
	cbz	w19, .LBB30_5
// %bb.3:
	mov	w0, w19
	ldp	x20, x19, [sp, #32]             // 16-byte Folded Reload
	ldr	x21, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #48             // 16-byte Folded Reload
	hint	#29
	ret
.LBB30_4:
	mov	w19, #65535
.LBB30_5:
	ldrh	w2, [x20, :lo12:gSysFreeQueue+6]
	adrp	x0, .L.str.86
	add	x0, x0, :lo12:.L.str.86
	mov	w1, w19
	bl	sftl_printk
.LBB30_6:                               // =>This Inner Loop Header: Depth=1
	b	.LBB30_6
.Lfunc_end30:
	.size	FtlFreeSysBlkQueueOut, .Lfunc_end30-FtlFreeSysBlkQueueOut
                                        // -- End function
	.globl	FtlVpcTblFlush                  // -- Begin function FtlVpcTblFlush
	.p2align	2
	.type	FtlVpcTblFlush,@function
FtlVpcTblFlush:                         // @FtlVpcTblFlush
// %bb.0:
	hint	#25
	sub	sp, sp, #112                    // =112
	adrp	x15, gSysInfo
	stp	x22, x21, [sp, #80]             // 16-byte Folded Spill
	adrp	x21, p_sys_data_buf
	adrp	x22, p_sys_spare_buf
	add	x15, x15, :lo12:gSysInfo
	ldr	x12, [x21, :lo12:p_sys_data_buf]
	ldr	x17, [x22, :lo12:p_sys_spare_buf]
	ldrh	w13, [x15]
	stp	x20, x19, [sp, #96]             // 16-byte Folded Spill
	adrp	x19, req_sys+8
	add	x19, x19, :lo12:req_sys+8
	mov	w8, #61604
	stp	x12, x17, [x19]
	strh	w13, [x17, #2]
	strh	w8, [x17]
	ldr	w8, [x15, #8]
	mov	x9, #19539
	movk	x9, #18004, lsl #16
	adrp	x20, g_sys_save_data
	movk	x9, #87, lsl #32
	add	x20, x20, :lo12:g_sys_save_data
	movk	x9, #20480, lsl #48
	adrp	x10, c_ftl_nand_die_num
	str	xzr, [x17, #8]
	str	w8, [x17, #4]
	ldrh	w8, [x15, #6]
	str	x9, [x20]
	ldrb	w9, [x10, :lo12:c_ftl_nand_die_num]
	adrp	x11, g_active_superblock
	add	x11, x11, :lo12:g_active_superblock
	ldrh	w10, [x11]
	strh	w8, [x20, #8]
	ldrh	w8, [x11, #2]
	strb	w9, [x20, #10]
	ldrb	w9, [x11, #6]
	ldrb	w11, [x11, #8]
	adrp	x14, g_buffer_superblock
	add	x14, x14, :lo12:g_buffer_superblock
	strh	w10, [x20, #14]
	ldrh	w10, [x14]
	orr	w8, w9, w8, lsl #6
	ldrh	w9, [x14, #2]
	strb	w11, [x20, #11]
	ldrb	w11, [x14, #6]
	ldrb	w13, [x14, #8]
	adrp	x12, g_gc_temp_superblock
	add	x12, x12, :lo12:g_gc_temp_superblock
	strh	w10, [x20, #18]
	ldrh	w10, [x12]
	orr	w9, w11, w9, lsl #6
	ldrh	w11, [x12, #2]
	strb	w13, [x20, #12]
	ldrb	w13, [x12, #6]
	ldrb	w12, [x12, #8]
	stp	x24, x23, [sp, #64]             // 16-byte Folded Spill
	adrp	x16, g_GlobalSysVersion
	orr	w11, w13, w11, lsl #6
	adrp	x13, g_totle_mlc_erase_count
	strb	w12, [x20, #13]
	adrp	x12, g_GlobalDataVersion
	adrp	x23, c_ftl_nand_byte_pre_page
	strh	w10, [x20, #22]
	ldr	w14, [x16, :lo12:g_GlobalSysVersion]
	ldr	w10, [x13, :lo12:g_totle_mlc_erase_count]
	ldr	w12, [x12, :lo12:g_GlobalDataVersion]
	ldr	x0, [x19]
	ldrh	w2, [x23, :lo12:c_ftl_nand_byte_pre_page]
	mov	w1, #255
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	stp	x28, x27, [sp, #32]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #48]             // 16-byte Folded Spill
	add	x29, sp, #16                    // =16
	str	x17, [sp, #8]                   // 8-byte Folded Spill
	strh	w8, [x20, #16]
	strh	w9, [x20, #20]
	strh	w11, [x20, #24]
	str	w10, [x20, #32]
	stp	w12, w14, [x20, #36]
	bl	memset
	ldr	x8, [x19]
	ldp	x9, x10, [x20]
	adrp	x19, p_valid_page_count_table
	add	x0, x8, #48                     // =48
	stp	x9, x10, [x8]
	ldp	x9, x10, [x20, #32]
	ldp	x11, x12, [x20, #16]
	adrp	x20, c_ftl_nand_data_blks_per_plane
	stp	x9, x10, [x8, #32]
	stp	x11, x12, [x8, #16]
	ldrh	w9, [x20, :lo12:c_ftl_nand_data_blks_per_plane]
	ldr	x1, [x19, :lo12:p_valid_page_count_table]
	lsl	x2, x9, #1
	bl	memcpy
	adrp	x9, FtlUpdateVaildLpnCount
	ldrh	w8, [x9, :lo12:FtlUpdateVaildLpnCount]
	add	w10, w8, #1                     // =1
	cmp	w8, #5                          // =5
	strh	w10, [x9, :lo12:FtlUpdateVaildLpnCount]
	b.lo	.LBB31_6
// %bb.1:
	ldrh	w8, [x20, :lo12:c_ftl_nand_data_blks_per_plane]
	strh	wzr, [x9, :lo12:FtlUpdateVaildLpnCount]
	adrp	x9, g_VaildLpn
	str	wzr, [x9, :lo12:g_VaildLpn]
	cbz	x8, .LBB31_6
// %bb.2:
	ldr	x11, [x19, :lo12:p_valid_page_count_table]
	mov	w10, wzr
	mov	w12, #65535
	b	.LBB31_4
.LBB31_3:                               //   in Loop: Header=BB31_4 Depth=1
	subs	x8, x8, #1                      // =1
	add	x11, x11, #2                    // =2
	b.eq	.LBB31_6
.LBB31_4:                               // =>This Inner Loop Header: Depth=1
	ldrh	w13, [x11]
	cmp	w13, w12
	b.eq	.LBB31_3
// %bb.5:                               //   in Loop: Header=BB31_4 Depth=1
	add	w10, w10, w13
	str	w10, [x9, :lo12:g_VaildLpn]
	b	.LBB31_3
.LBB31_6:
	adrp	x25, gSysInfo+2
	ldrh	w8, [x25, :lo12:gSysInfo+2]
	adrp	x24, req_sys+4
	adrp	x26, req_sys
	mov	w10, wzr
	add	x24, x24, :lo12:req_sys+4
	adrp	x27, gSysInfo
	adrp	x19, c_ftl_nand_page_pre_slc_blk
	add	x26, x26, :lo12:req_sys
	mov	w28, #65535
.LBB31_7:                               // =>This Loop Header: Depth=1
                                        //     Child Loop BB31_8 Depth 2
                                        //       Child Loop BB31_15 Depth 3
	str	w10, [sp, #4]                   // 4-byte Folded Spill
.LBB31_8:                               //   Parent Loop BB31_7 Depth=1
                                        // =>  This Loop Header: Depth=2
                                        //       Child Loop BB31_15 Depth 3
	ldrh	w12, [x19, :lo12:c_ftl_nand_page_pre_slc_blk]
	ldr	x11, [x22, :lo12:p_sys_spare_buf]
	ldrh	w10, [x27, :lo12:gSysInfo]
	ldr	x9, [x21, :lo12:p_sys_data_buf]
	and	w8, w8, #0xffff
	sub	w12, w12, #1                    // =1
	stur	x11, [x24, #12]
	orr	w11, w8, w10, lsl #10
	cmp	w12, w8
	stur	x9, [x24, #4]
	str	w11, [x24]
	b.gt	.LBB31_13
// %bb.9:                               //   in Loop: Header=BB31_8 Depth=2
	adrp	x9, gSysInfo+2
	adrp	x8, gSysFreeQueue+6
	add	x9, x9, :lo12:gSysInfo+2
	ldrh	w8, [x8, :lo12:gSysFreeQueue+6]
	ldrh	w28, [x9, #2]
	strh	w10, [x9, #2]
	strh	wzr, [x9]
	cbz	w8, .LBB31_31
// %bb.10:                              //   in Loop: Header=BB31_8 Depth=2
	adrp	x11, gSysFreeQueue
	add	x11, x11, :lo12:gSysFreeQueue
	ldrh	w9, [x11, #2]
	mov	x24, x26
	mov	x26, x19
	mov	x19, x25
	add	x10, x11, x9, lsl #1
	mov	x25, x22
	sub	w8, w8, #1                      // =1
	ldrh	w22, [x10, #8]
	adrp	x10, req_erase
	strh	w8, [x11, #6]
	add	w8, w9, #1                      // =1
	ldr	x9, [x10, :lo12:req_erase]
	mov	x20, x23
	mov	x23, x21
	and	w8, w8, #0x3ff
	lsl	w21, w22, #10
	strh	w8, [x11, #2]
	str	w21, [x9, #4]
	ldr	x0, [x10, :lo12:req_erase]
	mov	w2, #1
	bl	FlashEraseBlocks
	adrp	x9, g_totle_sys_slc_erase_count
	ldr	w8, [x9, :lo12:g_totle_sys_slc_erase_count]
	mov	w10, #65535
	cmp	w22, w10
	add	w8, w8, #1                      // =1
	str	w8, [x9, :lo12:g_totle_sys_slc_erase_count]
	b.eq	.LBB31_32
// %bb.11:                              //   in Loop: Header=BB31_8 Depth=2
	cbz	w22, .LBB31_32
// %bb.12:                              //   in Loop: Header=BB31_8 Depth=2
	adrp	x10, g_GlobalSysVersion
	ldr	w8, [x10, :lo12:g_GlobalSysVersion]
	adrp	x9, gSysInfo
	add	x9, x9, :lo12:gSysInfo
	strh	w22, [x9]
	str	w8, [x9, #8]
	add	w9, w8, #1                      // =1
	str	w9, [x10, :lo12:g_GlobalSysVersion]
	ldr	x9, [sp, #8]                    // 8-byte Folded Reload
	adrp	x11, req_sys+4
	str	w21, [x11, :lo12:req_sys+4]
	mov	x21, x23
	str	w8, [x9, #4]
	strh	w22, [x9, #2]
	ldr	x9, [x23, :lo12:p_sys_data_buf]
	mov	x22, x25
	mov	x25, x19
	mov	x19, x26
	mov	x26, x24
	adrp	x24, req_sys+4
	mov	x23, x20
	add	x24, x24, :lo12:req_sys+4
.LBB31_13:                              //   in Loop: Header=BB31_8 Depth=2
	ldrh	w8, [x23, :lo12:c_ftl_nand_byte_pre_page]
	mov	w10, #42982
	movk	w10, #18374, lsl #16
	cbz	x8, .LBB31_16
// %bb.14:                              //   in Loop: Header=BB31_8 Depth=2
	mov	w10, #42982
	movk	w10, #18374, lsl #16
.LBB31_15:                              //   Parent Loop BB31_7 Depth=1
                                        //     Parent Loop BB31_8 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	ldrb	w11, [x9], #1
	lsr	w12, w10, #2
	add	w12, w12, w10, lsl #5
	subs	x8, x8, #1                      // =1
	add	w11, w12, w11
	eor	w10, w11, w10
	b.ne	.LBB31_15
.LBB31_16:                              //   in Loop: Header=BB31_8 Depth=2
	ldr	x8, [sp, #8]                    // 8-byte Folded Reload
	mov	w1, #1
	mov	w2, #1
	mov	x0, x26
	str	w10, [x8, #12]
	bl	FlashProgPages
	ldrh	w9, [x25, :lo12:gSysInfo+2]
	ldr	w10, [x26]
	add	w8, w9, #1                      // =1
	cmn	w10, #1                         // =1
	strh	w8, [x25, :lo12:gSysInfo+2]
	b.eq	.LBB31_19
// %bb.17:                              //   in Loop: Header=BB31_8 Depth=2
	cbz	w9, .LBB31_8
// %bb.18:                              //   in Loop: Header=BB31_8 Depth=2
	cmp	w10, #256                       // =256
	b.eq	.LBB31_8
	b	.LBB31_26
.LBB31_19:                              //   in Loop: Header=BB31_7 Depth=1
	cbnz	w9, .LBB31_21
// %bb.20:                              //   in Loop: Header=BB31_7 Depth=1
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlVpcTblFlush
	mov	w2, #1145
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlVpcTblFlush
	bl	sftl_printk
	ldrh	w8, [x25, :lo12:gSysInfo+2]
.LBB31_21:                              //   in Loop: Header=BB31_7 Depth=1
	and	w9, w8, #0xffff
	cmp	w9, #1                          // =1
	b.ne	.LBB31_23
// %bb.22:                              //   in Loop: Header=BB31_7 Depth=1
	ldrh	w8, [x19, :lo12:c_ftl_nand_page_pre_slc_blk]
	sub	w8, w8, #1                      // =1
	strh	w8, [x25, :lo12:gSysInfo+2]
.LBB31_23:                              //   in Loop: Header=BB31_7 Depth=1
	ldr	w10, [sp, #4]                   // 4-byte Folded Reload
	and	w9, w10, #0xffff
	add	w10, w10, #1                    // =1
	cmp	w9, #3                          // =3
	b.lo	.LBB31_7
// %bb.24:
	adrp	x8, req_sys+4
	ldr	w1, [x8, :lo12:req_sys+4]
	adrp	x0, .L.str.95
	add	x0, x0, :lo12:.L.str.95
	mov	w2, #4
	bl	sftl_printk
.LBB31_25:                              // =>This Inner Loop Header: Depth=1
	b	.LBB31_25
.LBB31_26:
	mvn	w8, w28
	tst	w8, #0xffff
	b.eq	.LBB31_30
// %bb.27:
	tst	w28, #0xffff
	b.eq	.LBB31_30
// %bb.28:
	adrp	x8, gSysFreeQueue+6
	ldrh	w8, [x8, :lo12:gSysFreeQueue+6]
	cmp	w8, #1024                       // =1024
	b.eq	.LBB31_30
// %bb.29:
	adrp	x13, req_erase
	ldr	x10, [x13, :lo12:req_erase]
	and	w8, w28, #0xffff
	adrp	x9, c_ftl_nand_blks_per_die
	ldrh	w9, [x9, :lo12:c_ftl_nand_blks_per_die]
	adrp	x11, c_ftl_nand_planes_per_die
	lsl	w12, w8, #10
	ldrh	w11, [x11, :lo12:c_ftl_nand_planes_per_die]
	str	w12, [x10, #4]
	ldr	x0, [x13, :lo12:req_erase]
	udiv	w10, w8, w9
	msub	w8, w10, w9, w8
	mov	w2, #1
	udiv	w19, w8, w11
	bl	FlashEraseBlocks
	adrp	x8, p_erase_count_table
	ldr	x8, [x8, :lo12:p_erase_count_table]
	lsl	x9, x19, #1
	adrp	x12, g_totle_sys_slc_erase_count
	adrp	x11, gSysFreeQueue
	ldrh	w10, [x8, x9]
	add	x11, x11, :lo12:gSysFreeQueue
	add	w10, w10, #1                    // =1
	strh	w10, [x8, x9]
	ldr	w8, [x12, :lo12:g_totle_sys_slc_erase_count]
	ldrh	w9, [x11, #6]
	ldrh	w10, [x11, #4]
	add	w8, w8, #1                      // =1
	add	w9, w9, #1                      // =1
	str	w8, [x12, :lo12:g_totle_sys_slc_erase_count]
	add	x8, x11, x10, lsl #1
	add	w10, w10, #1                    // =1
	strh	w9, [x11, #6]
	strh	w28, [x8, #8]
	and	w8, w10, #0x3ff
	strh	w8, [x11, #4]
.LBB31_30:
	ldp	x20, x19, [sp, #96]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #80]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #64]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #48]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	add	sp, sp, #112                    // =112
	hint	#29
	ret
.LBB31_31:
	mov	w22, #65535
.LBB31_32:
	adrp	x8, gSysFreeQueue+6
	ldrh	w2, [x8, :lo12:gSysFreeQueue+6]
	adrp	x0, .L.str.86
	add	x0, x0, :lo12:.L.str.86
	mov	w1, w22
	bl	sftl_printk
.LBB31_33:                              // =>This Inner Loop Header: Depth=1
	b	.LBB31_33
.Lfunc_end31:
	.size	FtlVpcTblFlush, .Lfunc_end31-FtlVpcTblFlush
                                        // -- End function
	.globl	FtlSysBlkInit                   // -- Begin function FtlSysBlkInit
	.p2align	2
	.type	FtlSysBlkInit,@function
FtlSysBlkInit:                          // @FtlSysBlkInit
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-64]!           // 16-byte Folded Spill
	adrp	x8, c_ftl_nand_max_sys_blks
	ldr	w8, [x8, :lo12:c_ftl_nand_max_sys_blks]
	adrp	x9, gSysFreeQueue
	add	x9, x9, :lo12:gSysFreeQueue
	stp	x20, x19, [sp, #48]             // 16-byte Folded Spill
	adrp	x20, g_power_lost_recovery_flag
	add	x0, x9, #8                      // =8
	mov	w2, #2048
	mov	w1, wzr
	stp	x24, x23, [sp, #16]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #32]             // 16-byte Folded Spill
	mov	x29, sp
	strh	wzr, [x20, :lo12:g_power_lost_recovery_flag]
	stur	wzr, [x9, #2]
	strh	wzr, [x9, #6]
	strh	w8, [x9]
	bl	memset
	bl	FtlScanSysBlk
	adrp	x8, gSysInfo
	ldrsh	w8, [x8, :lo12:gSysInfo]
	cmn	w8, #1                          // =1
	b.eq	.LBB32_2
// %bb.1:
	bl	FtlLoadSysInfo
	cbz	w0, .LBB32_3
.LBB32_2:
	mov	w0, #-1
	b	.LBB32_48
.LBB32_3:
	mov	w21, #19539
	movk	w21, #18004, lsl #16
	bl	FtlLoadMapInfo
	adrp	x8, c_ftl_nand_max_vendor_blks
	adrp	x10, g_totle_vendor_block
	adrp	x11, c_ftl_nand_vendor_region_num
	ldrh	w8, [x8, :lo12:c_ftl_nand_max_vendor_blks]
	ldrh	w10, [x10, :lo12:g_totle_vendor_block]
	ldrh	w11, [x11, :lo12:c_ftl_nand_vendor_region_num]
	adrp	x0, gVendorBlkInfo
	add	x0, x0, :lo12:gVendorBlkInfo
	mov	w9, #61574
	strh	w9, [x0, #4]
	adrp	x9, p_vendor_block_table
	strh	w8, [x0, #10]
	adrp	x8, p_vendor_block_ver_table
	strh	w10, [x0, #8]
	adrp	x10, p_vendor_block_valid_page_count
	strh	w11, [x0, #6]
	adrp	x11, p_vendor_region_ppn_table
	ldr	x9, [x9, :lo12:p_vendor_block_table]
	ldr	x8, [x8, :lo12:p_vendor_block_ver_table]
	ldr	x10, [x10, :lo12:p_vendor_block_valid_page_count]
	ldr	x11, [x11, :lo12:p_vendor_region_ppn_table]
	stp	x9, x8, [x0, #16]
	stp	x10, x11, [x0, #32]
	bl	FtlMapTblRecovery
	adrp	x19, g_sys_ext_data
	add	x19, x19, :lo12:g_sys_ext_data
	mov	w1, #1
	mov	w0, wzr
	mov	x2, x19
	bl	FtlVendorPartRead
	ldr	w8, [x19]
	cmp	w8, w21
	b.ne	.LBB32_5
// %bb.4:
	adrp	x11, g_sys_ext_data+8
	add	x11, x11, :lo12:g_sys_ext_data+8
	ldp	w0, w18, [x11, #80]
	ldp	w17, w16, [x11]
	ldp	w15, w14, [x11, #8]
	ldp	w8, w13, [x11, #20]
	ldp	w12, w9, [x11, #28]
	ldp	w10, w11, [x11, #36]
	b	.LBB32_6
.LBB32_5:
	add	x0, x19, #4                     // =4
	mov	w2, #508
	mov	w1, wzr
	bl	memset
	mov	w11, wzr
	mov	w10, wzr
	mov	w9, wzr
	mov	w12, wzr
	mov	w13, wzr
	mov	w8, wzr
	mov	w14, wzr
	mov	w15, wzr
	mov	w16, wzr
	mov	w17, wzr
	mov	w18, wzr
	mov	w0, wzr
	str	w21, [x19]
.LBB32_6:
	adrp	x1, g_totle_write_sector
	str	w0, [x1, :lo12:g_totle_write_sector]
	adrp	x0, g_totle_read_sector
	adrp	x1, g_totle_gc_page_count
	str	w18, [x0, :lo12:g_totle_read_sector]
	adrp	x18, g_totle_write_page_count
	adrp	x0, g_totle_read_page_count
	str	w17, [x1, :lo12:g_totle_gc_page_count]
	adrp	x17, g_totle_l2p_write_count
	str	w16, [x18, :lo12:g_totle_write_page_count]
	adrp	x16, g_totle_sys_slc_erase_count
	str	w15, [x0, :lo12:g_totle_read_page_count]
	adrp	x15, g_totle_cache_write_count
	adrp	x0, g_max_erase_count
	str	w14, [x17, :lo12:g_totle_l2p_write_count]
	adrp	x17, g_totle_mlc_erase_count
	str	w13, [x16, :lo12:g_totle_sys_slc_erase_count]
	adrp	x13, c_mlc_erase_count_value
	adrp	x14, g_min_erase_count
	adrp	x21, c_ftl_nand_data_blks_per_plane
	str	w9, [x15, :lo12:g_totle_cache_write_count]
	str	w10, [x0, :lo12:g_max_erase_count]
	ldr	w9, [x17, :lo12:g_totle_mlc_erase_count]
	ldrh	w10, [x13, :lo12:c_mlc_erase_count_value]
	adrp	x1, g_totle_slc_erase_count
	adrp	x19, g_ect_tbl_info_size
	adrp	x22, gp_ect_tbl_info
	str	w11, [x14, :lo12:g_min_erase_count]
	ldrh	w11, [x21, :lo12:c_ftl_nand_data_blks_per_plane]
	str	w8, [x1, :lo12:g_totle_slc_erase_count]
	ldrh	w1, [x19, :lo12:g_ect_tbl_info_size]
	ldr	x2, [x22, :lo12:gp_ect_tbl_info]
	madd	w8, w9, w10, w8
	adrp	x18, g_totle_discard_page_count
	udiv	w8, w8, w11
	adrp	x9, g_totle_avg_erase_count
	mov	w0, #64
	str	w12, [x18, :lo12:g_totle_discard_page_count]
	str	w8, [x9, :lo12:g_totle_avg_erase_count]
	bl	FtlVendorPartRead
	ldr	x8, [x22, :lo12:gp_ect_tbl_info]
	mov	w9, #17221
	movk	w9, #16980, lsl #16
	ldr	w8, [x8]
	cmp	w8, w9
	b.eq	.LBB32_8
// %bb.7:
	adrp	x0, .L.str.75
	adrp	x1, .L.str.96
	add	x0, x0, :lo12:.L.str.75
	add	x1, x1, :lo12:.L.str.96
	bl	sftl_printk
	ldrh	w8, [x19, :lo12:g_ect_tbl_info_size]
	ldr	x0, [x22, :lo12:gp_ect_tbl_info]
	mov	w1, wzr
	lsl	x2, x8, #9
	bl	memset
.LBB32_8:
	adrp	x8, gSysFreeQueue+6
	ldrh	w8, [x8, :lo12:gSysFreeQueue+6]
	adrp	x19, g_sys_save_data+28
	cbz	w8, .LBB32_13
// %bb.9:
	ldrh	w8, [x19, :lo12:g_sys_save_data+28]
	and	w8, w8, #0x1f
	cbz	w8, .LBB32_13
// %bb.10:
	adrp	x9, gSysFreeQueue+2
	add	x9, x9, :lo12:gSysFreeQueue+2
	ldrh	w13, [x9]
	ldrh	w11, [x9, #2]
	adrp	x12, gSysFreeQueue+8
	mov	w10, wzr
	add	x12, x12, :lo12:gSysFreeQueue+8
.LBB32_11:                              // =>This Inner Loop Header: Depth=1
	and	x14, x13, #0xffff
	ldrh	w14, [x12, x14, lsl #1]
	add	w13, w13, #1                    // =1
	and	x15, x11, #0xffff
	add	w11, w11, #1                    // =1
	add	w10, w10, #1                    // =1
	and	w13, w13, #0x3ff
	cmp	w8, w10, uxth
	and	w11, w11, #0x3ff
	strh	w14, [x12, x15, lsl #1]
	b.hi	.LBB32_11
// %bb.12:
	strh	w13, [x9]
	strh	w11, [x9, #2]
.LBB32_13:
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	ldrh	w8, [x21, :lo12:c_ftl_nand_data_blks_per_plane]
	adrp	x9, FtlUpdateVaildLpnCount
	strh	wzr, [x9, :lo12:FtlUpdateVaildLpnCount]
	adrp	x9, g_VaildLpn
	adrp	x22, p_valid_page_count_table
	str	wzr, [x9, :lo12:g_VaildLpn]
	cbz	x8, .LBB32_18
// %bb.14:
	ldr	x11, [x22, :lo12:p_valid_page_count_table]
	mov	w10, wzr
	mov	w12, #65535
	b	.LBB32_16
.LBB32_15:                              //   in Loop: Header=BB32_16 Depth=1
	subs	x8, x8, #1                      // =1
	add	x11, x11, #2                    // =2
	b.eq	.LBB32_18
.LBB32_16:                              // =>This Inner Loop Header: Depth=1
	ldrh	w13, [x11]
	cmp	w13, w12
	b.eq	.LBB32_15
// %bb.17:                              //   in Loop: Header=BB32_16 Depth=1
	add	w10, w10, w13
	str	w10, [x9, :lo12:g_VaildLpn]
	b	.LBB32_15
.LBB32_18:
	adrp	x21, c_ftl_nand_l2pmap_ram_region_num
	ldrh	w8, [x21, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	cbz	w8, .LBB32_22
// %bb.19:
	adrp	x9, p_l2p_ram_map
	ldr	x11, [x9, :lo12:p_l2p_ram_map]
	cmp	w8, #1                          // =1
	mov	x10, xzr
	csinc	w9, w8, wzr, hi
	add	x11, x11, #4                    // =4
.LBB32_20:                              // =>This Inner Loop Header: Depth=1
	ldr	w12, [x11]
	tbnz	w12, #31, .LBB32_23
// %bb.21:                              //   in Loop: Header=BB32_20 Depth=1
	add	x10, x10, #1                    // =1
	cmp	x9, x10
	add	x11, x11, #16                   // =16
	b.ne	.LBB32_20
	b	.LBB32_24
.LBB32_22:
	mov	w9, wzr
	b	.LBB32_24
.LBB32_23:
	mov	w9, w10
.LBB32_24:
	ldrh	w10, [x19, :lo12:g_sys_save_data+28]
	cmp	w9, w8
	add	w8, w10, #1                     // =1
	strh	w8, [x19, :lo12:g_sys_save_data+28]
	adrp	x19, g_active_superblock
	add	x19, x19, :lo12:g_active_superblock
	b.lo	.LBB32_26
// %bb.25:
	ldrh	w8, [x20, :lo12:g_power_lost_recovery_flag]
	cbz	w8, .LBB32_32
.LBB32_26:
	mov	x0, x19
	bl	FtlSuperblockPowerLostFix
	adrp	x20, g_buffer_superblock
	add	x20, x20, :lo12:g_buffer_superblock
	mov	x0, x20
	bl	FtlSuperblockPowerLostFix
	ldrh	w8, [x19]
	ldr	x9, [x22, :lo12:p_valid_page_count_table]
	ldrh	w10, [x19, #4]
	lsl	x8, x8, #1
	ldrh	w11, [x9, x8]
	sub	w10, w11, w10
	adrp	x11, c_ftl_nand_page_pre_blk
	strh	w10, [x9, x8]
	ldrh	w8, [x11, :lo12:c_ftl_nand_page_pre_blk]
	ldrh	w9, [x20]
	ldr	x10, [x22, :lo12:p_valid_page_count_table]
	strb	wzr, [x19, #6]
	strh	wzr, [x19, #4]
	strh	w8, [x19, #2]
	lsl	x8, x9, #1
	ldrh	w12, [x20, #4]
	ldrh	w9, [x10, x8]
	sub	w9, w9, w12
	adrp	x12, g_sys_save_data+30
	strh	w9, [x10, x8]
	ldrh	w8, [x11, :lo12:c_ftl_nand_page_pre_blk]
	ldrh	w9, [x12, :lo12:g_sys_save_data+30]
	strb	wzr, [x20, #6]
	strh	wzr, [x20, #4]
	strh	w8, [x20, #2]
	add	w8, w9, #1                      // =1
	strh	w8, [x12, :lo12:g_sys_save_data+30]
	bl	FtlWriteDump_data
	ldrh	w8, [x21, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	cbz	w8, .LBB32_31
// %bb.27:
	adrp	x20, gL2pMapInfo
	mov	x22, xzr
	mov	x23, xzr
	adrp	x24, p_l2p_ram_map
	add	x20, x20, :lo12:gL2pMapInfo
	b	.LBB32_29
.LBB32_28:                              //   in Loop: Header=BB32_29 Depth=1
	add	x23, x23, #1                    // =1
	cmp	x23, w8, uxth
	add	x22, x22, #16                   // =16
	b.hs	.LBB32_31
.LBB32_29:                              // =>This Inner Loop Header: Depth=1
	ldr	x9, [x24, :lo12:p_l2p_ram_map]
	add	x9, x9, x22
	ldr	w10, [x9, #4]
	tbz	w10, #31, .LBB32_28
// %bb.30:                              //   in Loop: Header=BB32_29 Depth=1
	ldrh	w1, [x9]
	ldr	x2, [x9, #8]
	mov	x0, x20
	bl	FtlMapWritePage
	ldr	x8, [x24, :lo12:p_l2p_ram_map]
	add	x8, x8, x22
	ldr	w9, [x8, #4]
	and	w9, w9, #0x7fffffff
	str	w9, [x8, #4]
	ldrh	w8, [x21, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	b	.LBB32_28
.LBB32_31:
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.LBB32_32:
	ldrh	w20, [x19]
	mov	w8, #65535
	cmp	w20, w8
	b.eq	.LBB32_47
// %bb.33:
	adrp	x9, g_buffer_superblock+4
	ldrh	w8, [x19, #4]
	ldrh	w9, [x9, :lo12:g_buffer_superblock+4]
	orr	w8, w9, w8
	tst	w8, #0xffff
	b.ne	.LBB32_47
// %bb.34:
	adrp	x0, .L.str.112
	add	x0, x0, :lo12:.L.str.112
	mov	w1, w20
	bl	sftl_printk
	adrp	x22, g_gc_next_blk
	ldrh	w8, [x22, :lo12:g_gc_next_blk]
	adrp	x21, g_gc_next_blk_1
	cmp	w8, w20
	b.eq	.LBB32_40
// %bb.35:
	ldrh	w9, [x21, :lo12:g_gc_next_blk_1]
	cmp	w9, w20
	b.eq	.LBB32_40
// %bb.36:
	mov	w10, #65535
	cmp	w8, w10
	b.eq	.LBB32_39
// %bb.37:
	cmp	w9, w10
	b.ne	.LBB32_40
// %bb.38:
	strh	w20, [x21, :lo12:g_gc_next_blk_1]
	b	.LBB32_40
.LBB32_39:
	strh	w20, [x22, :lo12:g_gc_next_blk]
.LBB32_40:
	adrp	x8, g_buffer_superblock
	ldrh	w19, [x8, :lo12:g_buffer_superblock]
	adrp	x0, .L.str.112
	add	x0, x0, :lo12:.L.str.112
	mov	w1, w19
	bl	sftl_printk
	ldrh	w8, [x22, :lo12:g_gc_next_blk]
	cmp	w8, w19
	b.eq	.LBB32_46
// %bb.41:
	ldrh	w9, [x21, :lo12:g_gc_next_blk_1]
	cmp	w9, w19
	b.eq	.LBB32_46
// %bb.42:
	mov	w10, #65535
	cmp	w8, w10
	b.eq	.LBB32_45
// %bb.43:
	cmp	w9, w10
	b.ne	.LBB32_46
// %bb.44:
	strh	w19, [x21, :lo12:g_gc_next_blk_1]
	b	.LBB32_46
.LBB32_45:
	strh	w19, [x22, :lo12:g_gc_next_blk]
.LBB32_46:
	bl	FtlVpcTblFlush
	adrp	x0, g_active_superblock
	add	x0, x0, :lo12:g_active_superblock
	bl	allocate_new_data_superblock
	adrp	x0, g_buffer_superblock
	add	x0, x0, :lo12:g_buffer_superblock
	bl	allocate_new_data_superblock
.LBB32_47:
	bl	FtlVpcCheckAndModify
	mov	w0, wzr
.LBB32_48:
	ldp	x20, x19, [sp, #48]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #32]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #16]             // 16-byte Folded Reload
	ldp	x29, x30, [sp], #64             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end32:
	.size	FtlSysBlkInit, .Lfunc_end32-FtlSysBlkInit
                                        // -- End function
	.globl	sftl_init                       // -- Begin function sftl_init
	.p2align	2
	.type	sftl_init,@function
sftl_init:                              // @sftl_init
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	adrp	x0, .L.str.75
	adrp	x1, .L.str.1
	str	x19, [sp, #16]                  // 8-byte Folded Spill
	adrp	x19, gFtlInitStatus
	mov	w8, #-1
	add	x0, x0, :lo12:.L.str.75
	add	x1, x1, :lo12:.L.str.1
	mov	x29, sp
	str	w8, [x19, :lo12:gFtlInitStatus]
	bl	sftl_printk
	adrp	x0, g_nand_phy_info
	add	x0, x0, :lo12:g_nand_phy_info
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	adrp	x8, c_ftl_nand_max_sys_blks
	ldr	w8, [x8, :lo12:c_ftl_nand_max_sys_blks]
	adrp	x9, gSysFreeQueue
	add	x9, x9, :lo12:gSysFreeQueue
	add	x0, x9, #8                      // =8
	mov	w2, #2048
	mov	w1, wzr
	stur	wzr, [x9, #2]
	strh	wzr, [x9, #6]
	strh	w8, [x9]
	bl	memset
	bl	FtlLoadBbt
	cbnz	w0, .LBB33_5
// %bb.1:
	bl	FtlSysBlkInit
	cbnz	w0, .LBB33_5
// %bb.2:
	adrp	x8, g_num_free_superblocks
	ldrh	w8, [x8, :lo12:g_num_free_superblocks]
	mov	w9, #1
	str	w9, [x19, :lo12:gFtlInitStatus]
	cmp	w8, #15                         // =15
	b.hi	.LBB33_5
// %bb.3:
	mov	w19, #8129
.LBB33_4:                               // =>This Inner Loop Header: Depth=1
	mov	w0, wzr
	bl	rk_ftl_garbage_collect
	subs	w19, w19, #1                    // =1
	b.ne	.LBB33_4
.LBB33_5:
	ldr	x19, [sp, #16]                  // 8-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end33:
	.size	sftl_init, .Lfunc_end33-sftl_init
                                        // -- End function
	.globl	rk_ftl_garbage_collect          // -- Begin function rk_ftl_garbage_collect
	.p2align	2
	.type	rk_ftl_garbage_collect,@function
rk_ftl_garbage_collect:                 // @rk_ftl_garbage_collect
// %bb.0:
	hint	#25
	sub	sp, sp, #144                    // =144
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	stp	x29, x30, [sp, #48]             // 16-byte Folded Spill
	add	x29, sp, #48                    // =48
	stp	x28, x27, [sp, #64]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #80]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #96]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #112]            // 16-byte Folded Spill
	stp	x20, x19, [sp, #128]            // 16-byte Folded Spill
	stur	x8, [x29, #-8]
	adrp	x24, g_in_gc_progress
	ldr	w8, [x24, :lo12:g_in_gc_progress]
	mov	w20, wzr
	cbnz	w8, .LBB34_187
// %bb.1:
	adrp	x8, g_num_data_superblocks
	ldrh	w8, [x8, :lo12:g_num_data_superblocks]
	cmp	w8, #48                         // =48
	b.lo	.LBB34_187
// %bb.2:
	adrp	x8, ftl_gc_temp_block_bops_scan_page_addr
	ldrsh	w8, [x8, :lo12:ftl_gc_temp_block_bops_scan_page_addr]
	mov	w22, w0
	cmn	w8, #1                          // =1
	b.eq	.LBB34_5
// %bb.3:
	adrp	x8, g_gc_temp_superblock
	ldrh	w8, [x8, :lo12:g_gc_temp_superblock]
	mov	w9, #65535
	cmp	w8, w9
	b.eq	.LBB34_5
// %bb.4:
	mov	w0, #1
	mov	w20, #1
	bl	FtlGcFreeTempBlock
	cbnz	w0, .LBB34_187
.LBB34_5:
	adrp	x20, g_gc_next_blk_1
	ldrh	w8, [x20, :lo12:g_gc_next_blk_1]
	mov	w28, #65535
	adrp	x19, g_gc_next_blk
	cmp	w8, w28
	b.eq	.LBB34_8
// %bb.6:
	ldrh	w9, [x19, :lo12:g_gc_next_blk]
	cmp	w9, w28
	b.ne	.LBB34_8
// %bb.7:
	strh	w8, [x19, :lo12:g_gc_next_blk]
	mov	w8, #65535
	strh	w8, [x20, :lo12:g_gc_next_blk_1]
.LBB34_8:
	bl	FtlReadRefresh
	adrp	x27, g_gc_superblock
	adrp	x10, g_gc_temp_superblock
	adrp	x8, g_gc_skip_write_count
	ldrh	w10, [x10, :lo12:g_gc_temp_superblock]
	ldrh	w12, [x27, :lo12:g_gc_superblock]
	ldr	w11, [x8, :lo12:g_gc_skip_write_count]
	ldrh	w21, [x19, :lo12:g_gc_next_blk]
	mov	w9, #1
	bfi	w9, w22, #7, #25
	and	w10, w10, w12
	add	w9, w11, w9
	cmp	w10, w28
	adrp	x25, g_num_free_superblocks
	str	w9, [x8, :lo12:g_gc_skip_write_count]
	b.ne	.LBB34_35
// %bb.9:
	mov	w10, #65535
	cmp	w21, w10
	b.ne	.LBB34_35
// %bb.10:
	ldrh	w10, [x25, :lo12:g_num_free_superblocks]
	mov	w11, #1024
	cmp	w10, #24                        // =24
	mov	w10, #5120
	csel	w10, w10, w11, lo
	cmp	w9, w10
	b.ls	.LBB34_13
// %bb.11:
	str	wzr, [x8, :lo12:g_gc_skip_write_count]
	adrp	x8, gc_ink_free_return_value
	strh	wzr, [x8, :lo12:gc_ink_free_return_value]
	bl	GetSwlReplaceBlock
	ldrh	w2, [x25, :lo12:g_num_free_superblocks]
	mvn	w8, w0
	mov	w28, w0
	tst	w8, #0xffff
	b.eq	.LBB34_14
.LBB34_12:
	adrp	x8, p_valid_page_count_table
	adrp	x10, p_erase_count_table
	ldr	x8, [x8, :lo12:p_valid_page_count_table]
	ldr	x10, [x10, :lo12:p_erase_count_table]
	and	x9, x28, #0xffff
	adrp	x11, g_gc_free_blk_threshold
	lsl	x9, x9, #1
	ldrh	w3, [x8, x9]
	ldrh	w4, [x10, x9]
	ldrh	w5, [x11, :lo12:g_gc_free_blk_threshold]
	adrp	x0, .L.str.115
	and	w1, w28, #0xffff
	add	x0, x0, :lo12:.L.str.115
	bl	sftl_printk
	b	.LBB34_24
.LBB34_13:
	mov	w21, #65535
	mov	w28, #65535
	b	.LBB34_35
.LBB34_14:
	adrp	x8, g_gc_merge_free_blk_threshold
	ldrh	w9, [x8, :lo12:g_gc_merge_free_blk_threshold]
	cmp	w2, w9
	b.hs	.LBB34_21
// %bb.15:
	adrp	x9, p_data_block_list_head
	ldr	x9, [x9, :lo12:p_data_block_list_head]
	cbz	x9, .LBB34_23
// %bb.16:
	adrp	x10, p_data_block_list_table
	ldr	x10, [x10, :lo12:p_data_block_list_table]
	mov	w11, #64
	mov	w12, #6
	mov	x14, x9
.LBB34_17:                              // =>This Inner Loop Header: Depth=1
	ldrh	w13, [x14]
	mov	w14, #65535
	cmp	x13, x14
	b.eq	.LBB34_199
// %bb.18:                              //   in Loop: Header=BB34_17 Depth=1
	sub	w11, w11, #1                    // =1
	tst	w11, #0xffff
	madd	x14, x13, x12, x10
	b.ne	.LBB34_17
// %bb.19:
	adrp	x11, p_valid_page_count_table
	ldr	x11, [x11, :lo12:p_valid_page_count_table]
	ldrh	w11, [x11, x13, lsl #1]
	cmp	w11, #8                         // =8
	b.hs	.LBB34_203
// %bb.20:
	sub	x9, x9, x10
	mov	w10, #43691
	movk	w10, #43690, lsl #16
	lsr	x9, x9, #1
	mul	w28, w9, w10
	mvn	w9, w28
	tst	w9, #0xffff
	mov	w9, #128
	strh	w9, [x8, :lo12:g_gc_merge_free_blk_threshold]
	b.ne	.LBB34_12
	b	.LBB34_23
.LBB34_21:
	mov	w9, #80
.LBB34_22:
	strh	w9, [x8, :lo12:g_gc_merge_free_blk_threshold]
.LBB34_23:
	mov	w28, #65535
.LBB34_24:
	adrp	x8, g_gc_bad_block_temp_num
	ldrh	w9, [x8, :lo12:g_gc_bad_block_temp_num]
	ldrh	w21, [x19, :lo12:g_gc_next_blk]
	cbz	w9, .LBB34_35
// %bb.25:
	mov	w8, #65535
	cmp	w21, w8
	b.ne	.LBB34_35
// %bb.26:
	mov	w23, w22
	adrp	x22, g_gc_bad_block_gc_index
	ldrh	w8, [x22, :lo12:g_gc_bad_block_gc_index]
	cmp	w8, w9
	b.lo	.LBB34_28
// %bb.27:
	mov	w8, wzr
	strh	wzr, [x22, :lo12:g_gc_bad_block_gc_index]
.LBB34_28:
	adrp	x9, g_gc_bad_block_temp_tbl
	add	x9, x9, :lo12:g_gc_bad_block_temp_tbl
	adrp	x10, c_ftl_nand_blks_per_die
	ldrh	w8, [x9, w8, uxtw #1]
	ldrh	w9, [x10, :lo12:c_ftl_nand_blks_per_die]
	adrp	x10, c_ftl_nand_planes_per_die
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_per_die]
	adrp	x0, .L.str.112
	udiv	w11, w8, w9
	msub	w8, w11, w9, w8
	udiv	w21, w8, w10
	add	x0, x0, :lo12:.L.str.112
	mov	w1, w21
	bl	sftl_printk
	ldrh	w8, [x19, :lo12:g_gc_next_blk]
	cmp	w8, w21
	b.eq	.LBB34_34
// %bb.29:
	ldrh	w9, [x20, :lo12:g_gc_next_blk_1]
	cmp	w9, w21, uxth
	b.eq	.LBB34_33
// %bb.30:
	mov	w10, #65535
	cmp	w8, w10
	b.eq	.LBB34_200
// %bb.31:
	cmp	w9, w10
	b.ne	.LBB34_33
// %bb.32:
	strh	w21, [x20, :lo12:g_gc_next_blk_1]
.LBB34_33:
	mov	w21, w8
.LBB34_34:
	ldrh	w8, [x22, :lo12:g_gc_bad_block_gc_index]
	add	w8, w8, #1                      // =1
	strh	w8, [x22, :lo12:g_gc_bad_block_gc_index]
	mov	w22, w23
.LBB34_35:
	adrp	x12, g_in_swl_replace
	cbnz	w22, .LBB34_39
// %bb.36:
	mvn	w8, w28
	tst	w8, #0xffff
	b.ne	.LBB34_39
// %bb.37:
	ldrh	w9, [x25, :lo12:g_num_free_superblocks]
	cmp	w9, #24                         // =24
	b.ls	.LBB34_47
// %bb.38:
	mov	w10, #1
	b	.LBB34_52
.LBB34_39:
	adrp	x8, g_gc_temp_superblock
	ldrh	w9, [x8, :lo12:g_gc_temp_superblock]
	ldrh	w8, [x27, :lo12:g_gc_superblock]
	mov	w26, #1
	and	w10, w21, w9
	mov	w9, #65535
	cmp	w10, w9
	b.ne	.LBB34_74
// %bb.40:
	and	w10, w8, w28
	cmp	w10, w9
	b.ne	.LBB34_74
// %bb.41:
	adrp	x21, g_gc_free_blk_threshold
	ldrh	w8, [x25, :lo12:g_num_free_superblocks]
	ldrh	w9, [x21, :lo12:g_gc_free_blk_threshold]
	mov	x23, x12
	adrp	x20, gc_ink_free_return_value
	str	wzr, [x12, :lo12:g_in_swl_replace]
	cmp	w8, w9
	b.ls	.LBB34_63
// %bb.42:
	ldrh	w8, [x20, :lo12:gc_ink_free_return_value]
	cbnz	w8, .LBB34_44
// %bb.43:
	adrp	x9, g_MaxLpn
	adrp	x8, g_VaildLpn
	ldr	w9, [x9, :lo12:g_MaxLpn]
	ldr	w8, [x8, :lo12:g_VaildLpn]
	add	w9, w9, w9, lsl #1
	cmp	w8, w9, lsr #2
	b.hs	.LBB34_189
.LBB34_44:
	adrp	x8, c_ftl_nand_data_op_blks_per_plane
	ldrh	w8, [x8, :lo12:c_ftl_nand_data_op_blks_per_plane]
	add	w8, w8, w8, lsl #1
	lsr	w8, w8, #2
.LBB34_45:
	strh	w8, [x21, :lo12:g_gc_free_blk_threshold]
	bl	FtlReadRefresh
	adrp	x8, p_data_block_list_head
	ldr	x8, [x8, :lo12:p_data_block_list_head]
	cbz	x8, .LBB34_60
// %bb.46:
	adrp	x9, p_data_block_list_table
	ldr	w9, [x9, :lo12:p_data_block_list_table]
	sub	w8, w8, w9
	mov	w9, #43691
	lsr	w8, w8, #1
	movk	w9, #43690, lsl #16
	mul	w8, w8, w9
	and	x8, x8, #0xffff
	b	.LBB34_61
.LBB34_47:
	adrp	x8, c_ftl_nand_page_pre_blk
	cmp	w9, #17                         // =17
	b.lo	.LBB34_49
// %bb.48:
	ldrh	w8, [x8, :lo12:c_ftl_nand_page_pre_blk]
	lsr	w10, w8, #5
	b	.LBB34_52
.LBB34_49:
	cmp	w9, #13                         // =13
	b.lo	.LBB34_51
// %bb.50:
	ldrh	w8, [x8, :lo12:c_ftl_nand_page_pre_blk]
	lsr	w10, w8, #4
	b	.LBB34_52
.LBB34_51:
	ldrh	w8, [x8, :lo12:c_ftl_nand_page_pre_blk]
	cmp	w9, #8                          // =8
	lsr	w10, w8, #2
	csel	w10, w10, w8, hi
.LBB34_52:
	adrp	x8, g_gc_free_blk_threshold
	ldrh	w11, [x8, :lo12:g_gc_free_blk_threshold]
	cmp	w9, w11
	b.ls	.LBB34_55
// %bb.53:
	adrp	x9, g_gc_temp_superblock
	ldrh	w9, [x9, :lo12:g_gc_temp_superblock]
	mov	w11, #65535
	and	w9, w21, w9
	cmp	w9, w11
	b.eq	.LBB34_56
// %bb.54:
	adrp	x9, c_ftl_nand_data_op_blks_per_plane
	ldrh	w9, [x9, :lo12:c_ftl_nand_data_op_blks_per_plane]
	add	w9, w9, w9, lsl #1
	lsr	w9, w9, #2
	strh	w9, [x8, :lo12:g_gc_free_blk_threshold]
.LBB34_55:
	adrp	x8, g_gc_bad_block_temp_num
	ldrh	w8, [x8, :lo12:g_gc_bad_block_temp_num]
	add	w9, w10, #32                    // =32
	cmp	w8, #0                          // =0
	csel	w26, w10, w9, eq
	b	.LBB34_73
.LBB34_56:
	adrp	x9, gc_ink_free_return_value
	ldrh	w20, [x9, :lo12:gc_ink_free_return_value]
	mov	x11, x12
	cbnz	w20, .LBB34_58
// %bb.57:
	adrp	x10, g_MaxLpn
	adrp	x9, g_VaildLpn
	ldr	w10, [x10, :lo12:g_MaxLpn]
	ldr	w9, [x9, :lo12:g_VaildLpn]
	add	w10, w10, w10, lsl #1
	cmp	w9, w10, lsr #2
	b.hs	.LBB34_195
.LBB34_58:
	adrp	x9, c_ftl_nand_data_op_blks_per_plane
	ldrh	w9, [x9, :lo12:c_ftl_nand_data_op_blks_per_plane]
	add	w9, w9, w9, lsl #1
	lsr	w9, w9, #2
.LBB34_59:
	strh	w9, [x8, :lo12:g_gc_free_blk_threshold]
	str	wzr, [x11, :lo12:g_in_swl_replace]
	b	.LBB34_187
.LBB34_60:
	mov	w8, #65535
.LBB34_61:
	adrp	x9, p_valid_page_count_table
	ldr	x9, [x9, :lo12:p_valid_page_count_table]
	ldrh	w8, [x9, x8, lsl #1]
	cmp	w8, #5                          // =5
	b.lo	.LBB34_63
// %bb.62:
	ldrh	w20, [x20, :lo12:gc_ink_free_return_value]
	b	.LBB34_187
.LBB34_63:
	ldrh	w8, [x20, :lo12:gc_ink_free_return_value]
	cbz	w8, .LBB34_65
// %bb.64:
	mov	w26, #1
	b	.LBB34_72
.LBB34_65:
	adrp	x8, c_ftl_nand_data_op_blks_per_plane
	ldrh	w8, [x8, :lo12:c_ftl_nand_data_op_blks_per_plane]
	mov	w13, w22
	adrp	x22, p_data_block_list_head
	ldr	x9, [x22, :lo12:p_data_block_list_head]
	add	w10, w8, w8, lsl #1
	lsr	w10, w10, #2
	strh	w10, [x21, :lo12:g_gc_free_blk_threshold]
	cbz	x9, .LBB34_67
// %bb.66:
	adrp	x10, p_data_block_list_table
	ldr	w10, [x10, :lo12:p_data_block_list_table]
	sub	w9, w9, w10
	mov	w10, #43691
	lsr	w9, w9, #1
	movk	w10, #43690, lsl #16
	mul	w9, w9, w10
	and	x9, x9, #0xffff
	b	.LBB34_68
.LBB34_67:
	mov	w9, #65535
.LBB34_68:
	adrp	x21, p_valid_page_count_table
	adrp	x10, c_ftl_nand_page_pre_slc_blk
	ldr	x11, [x21, :lo12:p_valid_page_count_table]
	adrp	x12, c_ftl_nand_planes_num
	ldrh	w10, [x10, :lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w12, [x12, :lo12:c_ftl_nand_planes_num]
	ldrh	w9, [x11, x9, lsl #1]
	mul	w10, w12, w10
	cmp	w10, #0                         // =0
	cinc	w10, w10, lt
	cmp	w9, w10, asr #1
	b.le	.LBB34_70
// %bb.69:
	ldrh	w10, [x25, :lo12:g_num_free_superblocks]
	sub	w8, w8, #1                      // =1
	cmp	w8, w10
	b.le	.LBB34_196
.LBB34_70:
	cbz	w9, .LBB34_190
// %bb.71:
	mov	w26, #1
	mov	w22, w13
.LBB34_72:
	mov	x12, x23
.LBB34_73:
	ldrh	w8, [x27, :lo12:g_gc_superblock]
.LBB34_74:
	mov	w9, #65535
	adrp	x10, g_gc_superblock+8
	cmp	w8, w9
	adrp	x23, g_gc_cur_blk_max_valid_pages
	b.ne	.LBB34_90
// %bb.75:
	mvn	w8, w28
	tst	w8, #0xffff
	b.eq	.LBB34_83
// %bb.76:
	strh	w28, [x27, :lo12:g_gc_superblock]
	mov	w8, w28
.LBB34_77:
	adrp	x9, g_gc_blk_num
	ldrh	w9, [x9, :lo12:g_gc_blk_num]
	strb	wzr, [x10, :lo12:g_gc_superblock+8]
	cbz	x9, .LBB34_82
// %bb.78:
	adrp	x10, p_gc_blk_tbl
	ldr	x10, [x10, :lo12:p_gc_blk_tbl]
.LBB34_79:                              // =>This Inner Loop Header: Depth=1
	ldrh	w11, [x10]
	cmp	w11, w8, uxth
	b.eq	.LBB34_86
// %bb.80:                              //   in Loop: Header=BB34_79 Depth=1
	subs	x9, x9, #1                      // =1
	add	x10, x10, #2                    // =2
	b.ne	.LBB34_79
// %bb.81:
	mvn	w8, w8
	tst	w8, #0xffff
	b.eq	.LBB34_88
.LBB34_82:
	adrp	x19, g_gc_superblock
	add	x19, x19, :lo12:g_gc_superblock
	mov	x0, x19
	mov	x20, x12
	bl	make_superblock
	adrp	x8, p_valid_page_count_table
	ldr	x9, [x8, :lo12:p_valid_page_count_table]
	ldrh	w8, [x19]
	adrp	x10, g_gc_cur_blk_valid_pages
	strh	wzr, [x19, #2]
	strb	wzr, [x19, #6]
	strh	wzr, [x10, :lo12:g_gc_cur_blk_valid_pages]
	ldrh	w9, [x9, x8, lsl #1]
	mov	x12, x20
	strh	w9, [x23, :lo12:g_gc_cur_blk_max_valid_pages]
	b	.LBB34_90
.LBB34_83:
	ldrh	w8, [x19, :lo12:g_gc_next_blk]
	mov	w9, #65535
	cmp	x8, x9
	b.eq	.LBB34_87
// %bb.84:
	adrp	x9, p_valid_page_count_table
	ldr	x9, [x9, :lo12:p_valid_page_count_table]
	ldrh	w9, [x9, x8, lsl #1]
	cbz	w9, .LBB34_89
// %bb.85:
	mov	w9, #65535
	strh	w8, [x27, :lo12:g_gc_superblock]
	strh	w9, [x19, :lo12:g_gc_next_blk]
	b	.LBB34_77
.LBB34_86:
	mov	w8, #65535
	strh	w8, [x27, :lo12:g_gc_superblock]
	b	.LBB34_90
.LBB34_87:
	strb	wzr, [x10, :lo12:g_gc_superblock+8]
.LBB34_88:
	mov	w8, #65535
	b	.LBB34_90
.LBB34_89:
	adrp	x9, g_gc_superblock
	mov	w8, #65535
	add	x9, x9, :lo12:g_gc_superblock
	strh	w8, [x9]
	strh	w8, [x19, :lo12:g_gc_next_blk]
	strb	wzr, [x9, #8]
.LBB34_90:
	adrp	x9, g_active_superblock
	ldrh	w9, [x9, :lo12:g_active_superblock]
	str	x28, [sp, #16]                  // 8-byte Folded Spill
	cmp	w8, w9
	adrp	x9, g_buffer_superblock
	b.eq	.LBB34_93
// %bb.91:
	ldrh	w9, [x9, :lo12:g_buffer_superblock]
	cmp	w8, w9
	b.eq	.LBB34_93
// %bb.92:
	adrp	x9, g_gc_temp_superblock
	ldrh	w9, [x9, :lo12:g_gc_temp_superblock]
	cmp	w8, w9
	b.ne	.LBB34_94
.LBB34_93:
	mov	w8, #65535
	strh	w8, [x27, :lo12:g_gc_superblock]
.LBB34_94:
	mov	w18, #65535
	adrp	x17, c_ftl_nand_planes_num
	mov	w20, #6
	adrp	x13, g_gc_superblock+2
	adrp	x28, req_gc
	mov	w23, #24
                                        // kill: def $w8 killed $w8 killed $x8 def $x8
.LBB34_95:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB34_125 Depth 2
                                        //       Child Loop BB34_127 Depth 3
                                        //       Child Loop BB34_132 Depth 3
                                        //     Child Loop BB34_101 Depth 2
                                        //       Child Loop BB34_104 Depth 3
                                        //       Child Loop BB34_111 Depth 3
                                        //         Child Loop BB34_118 Depth 4
                                        //     Child Loop BB34_161 Depth 2
                                        //     Child Loop BB34_168 Depth 2
	mvn	w9, w8
	tst	w9, #0xffff
	b.eq	.LBB34_123
.LBB34_96:                              //   in Loop: Header=BB34_95 Depth=1
	adrp	x9, c_ftl_nand_page_pre_blk
	ldrh	w11, [x9, :lo12:c_ftl_nand_page_pre_blk]
	mov	w9, #1
	str	w9, [x24, :lo12:g_in_gc_progress]
	cbz	w22, .LBB34_98
// %bb.97:                              //   in Loop: Header=BB34_95 Depth=1
	adrp	x9, p_valid_page_count_table
	ldr	x9, [x9, :lo12:p_valid_page_count_table]
                                        // kill: def $w8 killed $w8 killed $x8 def $x8
	and	x8, x8, #0xffff
	ldrh	w8, [x9, x8, lsl #1]
	ldrh	w9, [x17, :lo12:c_ftl_nand_planes_num]
	neg	w8, w8
	madd	w8, w9, w11, w8
	add	w9, w8, #3                      // =3
	cmp	w8, #0                          // =0
	csel	w8, w9, w8, lt
	add	w26, w26, w8, lsr #2
.LBB34_98:                              //   in Loop: Header=BB34_95 Depth=1
	ldrh	w8, [x13, :lo12:g_gc_superblock+2]
	add	w9, w8, w26, uxth
	sub	w10, w11, w8
	cmp	w9, w11
	csel	w9, w10, w26, hi
	tst	w9, #0xffff
	b.eq	.LBB34_153
// %bb.99:                              //   in Loop: Header=BB34_95 Depth=1
	stp	w11, w22, [sp, #8]              // 8-byte Folded Spill
	mov	w22, wzr
	and	w8, w9, #0xffff
	str	w9, [sp, #4]                    // 4-byte Folded Spill
	stur	w8, [x29, #-20]                 // 4-byte Folded Spill
	b	.LBB34_101
.LBB34_100:                             //   in Loop: Header=BB34_101 Depth=2
	ldur	w8, [x29, #-20]                 // 4-byte Folded Reload
	add	w22, w22, #1                    // =1
	adrp	x17, c_ftl_nand_planes_num
	adrp	x13, g_gc_superblock+2
	cmp	w22, w8
	mov	w18, #65535
	b.eq	.LBB34_152
.LBB34_101:                             //   Parent Loop BB34_95 Depth=1
                                        // =>  This Loop Header: Depth=2
                                        //       Child Loop BB34_104 Depth 3
                                        //       Child Loop BB34_111 Depth 3
                                        //         Child Loop BB34_118 Depth 4
	ldrh	w9, [x17, :lo12:c_ftl_nand_planes_num]
	cbz	w9, .LBB34_106
// %bb.102:                             //   in Loop: Header=BB34_101 Depth=2
	adrp	x14, g_gc_superblock
	mov	x8, xzr
	mov	w20, wzr
	add	x14, x14, :lo12:g_gc_superblock
	b	.LBB34_104
.LBB34_103:                             //   in Loop: Header=BB34_104 Depth=3
	add	x8, x8, #1                      // =1
	cmp	x8, w9, uxth
	b.hs	.LBB34_107
.LBB34_104:                             //   Parent Loop BB34_95 Depth=1
                                        //     Parent Loop BB34_101 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	add	x10, x14, x8, lsl #1
	ldrh	w10, [x10, #16]
	cmp	w10, w18
	b.eq	.LBB34_103
// %bb.105:                             //   in Loop: Header=BB34_104 Depth=3
	ldrh	w9, [x13, :lo12:g_gc_superblock+2]
	ldr	x11, [x28, :lo12:req_gc]
	and	x12, x20, #0xffff
	add	w20, w20, #1                    // =1
	add	w9, w22, w9
	orr	w9, w9, w10, lsl #10
	add	x10, x11, x12, lsl #5
	str	w9, [x10, #4]
	ldrh	w9, [x17, :lo12:c_ftl_nand_planes_num]
	b	.LBB34_103
.LBB34_106:                             //   in Loop: Header=BB34_101 Depth=2
	mov	w20, wzr
.LBB34_107:                             //   in Loop: Header=BB34_101 Depth=2
	ldr	x0, [x28, :lo12:req_gc]
	and	w26, w20, #0xffff
	mov	w1, w26
	bl	FlashReadPages
	cbz	w26, .LBB34_100
// %bb.108:                             //   in Loop: Header=BB34_101 Depth=2
                                        // kill: def $w20 killed $w20 killed $x20 def $x20
	mov	x24, xzr
	and	x20, x20, #0xffff
	b	.LBB34_111
.LBB34_109:                             //   in Loop: Header=BB34_111 Depth=3
	bl	Ftl_gc_temp_data_write_back
	cbnz	w0, .LBB34_186
.LBB34_110:                             //   in Loop: Header=BB34_111 Depth=3
	add	x24, x24, #1                    // =1
	cmp	x24, x20
	b.eq	.LBB34_100
.LBB34_111:                             //   Parent Loop BB34_95 Depth=1
                                        //     Parent Loop BB34_101 Depth=2
                                        // =>    This Loop Header: Depth=3
                                        //         Child Loop BB34_118 Depth 4
	ldr	x8, [x28, :lo12:req_gc]
	add	x8, x8, x24, lsl #5
	ldr	w9, [x8]
	cmn	w9, #1                          // =1
	b.eq	.LBB34_110
// %bb.112:                             //   in Loop: Header=BB34_111 Depth=3
	ldr	x27, [x8, #16]
	ldrsh	w8, [x27]
	cmn	w8, #3947                       // =3947
	b.ne	.LBB34_110
// %bb.113:                             //   in Loop: Header=BB34_111 Depth=3
	ldr	w26, [x27, #8]
	cmn	w26, #1                         // =1
	b.ne	.LBB34_115
// %bb.114:                             //   in Loop: Header=BB34_111 Depth=3
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.rk_ftl_garbage_collect
	mov	w2, #753
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.rk_ftl_garbage_collect
	bl	sftl_printk
.LBB34_115:                             //   in Loop: Header=BB34_111 Depth=3
	sub	x1, x29, #12                    // =12
	mov	w0, w26
	mov	w2, wzr
	bl	log2phys
	ldr	x8, [x28, :lo12:req_gc]
	ldur	w26, [x29, #-12]
	add	x9, x8, x24, lsl #5
	ldr	w9, [x9, #4]
	cmp	w26, w9
	b.ne	.LBB34_110
// %bb.116:                             //   in Loop: Header=BB34_111 Depth=3
	adrp	x11, g_gc_cur_blk_valid_pages
	ldrh	w9, [x11, :lo12:g_gc_cur_blk_valid_pages]
	lsl	x19, x24, #5
	adrp	x25, req_gc_dst
	adrp	x21, g_gc_num_req
	add	w9, w9, #1                      // =1
	ldr	x10, [x25, :lo12:req_gc_dst]
	add	x8, x8, x19
	strh	w9, [x11, :lo12:g_gc_cur_blk_valid_pages]
	ldr	w9, [x21, :lo12:g_gc_num_req]
	ldr	w8, [x8, #24]
	add	x9, x10, x9, lsl #5
	str	w8, [x9, #24]
	bl	Ftl_get_new_temp_ppa
	ldr	x8, [x25, :lo12:req_gc_dst]
	ldr	w9, [x21, :lo12:g_gc_num_req]
	add	x8, x8, x9, lsl #5
	str	w0, [x8, #4]
	ldr	x8, [x28, :lo12:req_gc]
	ldr	w10, [x21, :lo12:g_gc_num_req]
	ldr	x9, [x25, :lo12:req_gc_dst]
	add	x8, x8, x19
	ldr	x8, [x8, #8]
	lsl	x10, x10, #5
	add	x9, x9, x10
	str	x8, [x9, #8]
	ldr	x8, [x28, :lo12:req_gc]
	ldr	x9, [x25, :lo12:req_gc_dst]
	add	x8, x8, x19
	ldr	x8, [x8, #16]
	add	x9, x9, x10
	str	x8, [x9, #16]
	str	w26, [x27, #12]
	adrp	x8, g_gc_temp_superblock
	ldrh	w8, [x8, :lo12:g_gc_temp_superblock]
	adrp	x9, c_gc_page_buf_num
	strh	w8, [x27, #2]
	adrp	x8, g_GlobalDataVersion
	ldr	w8, [x8, :lo12:g_GlobalDataVersion]
	str	w8, [x27, #4]
	ldr	w8, [x21, :lo12:g_gc_num_req]
	ldr	w10, [x9, :lo12:c_gc_page_buf_num]
	add	w8, w8, #1                      // =1
	str	w8, [x21, :lo12:g_gc_num_req]
	cbz	w10, .LBB34_121
// %bb.117:                             //   in Loop: Header=BB34_111 Depth=3
	adrp	x11, gp_gc_page_buf_info
	ldr	x9, [x28, :lo12:req_gc]
	ldr	x11, [x11, :lo12:gp_gc_page_buf_info]
	mov	w12, wzr
.LBB34_118:                             //   Parent Loop BB34_95 Depth=1
                                        //     Parent Loop BB34_101 Depth=2
                                        //       Parent Loop BB34_111 Depth=3
                                        // =>      This Inner Loop Header: Depth=4
	and	x13, x12, #0xffff
	madd	x13, x13, x23, x11
	ldr	w14, [x13, #16]!
	cbz	w14, .LBB34_120
// %bb.119:                             //   in Loop: Header=BB34_118 Depth=4
	add	w12, w12, #1                    // =1
	cmp	w10, w12, uxth
	b.hi	.LBB34_118
	b	.LBB34_121
.LBB34_120:                             //   in Loop: Header=BB34_111 Depth=3
	mov	w8, #1
	str	w8, [x13]
	adrp	x11, gp_gc_page_buf_info
	ldr	x8, [x11, :lo12:gp_gc_page_buf_info]
                                        // kill: def $w12 killed $w12 killed $x12 def $x12
	and	x10, x12, #0xffff
	add	x10, x10, w12, uxth #1
	lsl	x10, x10, #3
	ldr	x8, [x8, x10]
	add	x9, x9, x24, lsl #5
	str	x8, [x9, #8]
	ldr	x8, [x11, :lo12:gp_gc_page_buf_info]
	add	x8, x8, x10
	ldr	x8, [x8, #8]
	str	x8, [x9, #16]
	adrp	x8, g_gc_num_req
	ldr	w8, [x8, :lo12:g_gc_num_req]
.LBB34_121:                             //   in Loop: Header=BB34_111 Depth=3
	adrp	x9, g_gc_temp_superblock+4
	add	x9, x9, :lo12:g_gc_temp_superblock+4
	ldrb	w9, [x9, #3]
	cmp	w8, w9
	b.eq	.LBB34_109
// %bb.122:                             //   in Loop: Header=BB34_111 Depth=3
	adrp	x8, g_gc_temp_superblock+4
	add	x8, x8, :lo12:g_gc_temp_superblock+4
	ldrh	w8, [x8]
	cbnz	w8, .LBB34_110
	b	.LBB34_109
.LBB34_123:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x8, p_data_block_list_head
	ldr	x8, [x8, :lo12:p_data_block_list_head]
	str	wzr, [x12, :lo12:g_in_swl_replace]
	cbz	x8, .LBB34_184
// %bb.124:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x9, g_gc_blk_index
	ldrh	w10, [x9, :lo12:g_gc_blk_index]
	mov	x25, x12
.LBB34_125:                             //   Parent Loop BB34_95 Depth=1
                                        // =>  This Loop Header: Depth=2
                                        //       Child Loop BB34_127 Depth 3
                                        //       Child Loop BB34_132 Depth 3
	adrp	x9, p_data_block_list_table
	ldr	x9, [x9, :lo12:p_data_block_list_table]
	tst	w10, #0xffff
	mov	x12, x8
	b.eq	.LBB34_129
// %bb.126:                             //   in Loop: Header=BB34_125 Depth=2
	mov	x12, x8
	mov	w11, w10
.LBB34_127:                             //   Parent Loop BB34_95 Depth=1
                                        //     Parent Loop BB34_125 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	ldrh	w12, [x12]
	cmp	x12, x18
	b.eq	.LBB34_184
// %bb.128:                             //   in Loop: Header=BB34_127 Depth=3
	sub	w11, w11, #1                    // =1
	tst	w11, #0xffff
	madd	x12, x12, x20, x9
	b.ne	.LBB34_127
.LBB34_129:                             //   in Loop: Header=BB34_125 Depth=2
	sub	x9, x12, x9
	mov	x11, #-6148914691236517206
	asr	x9, x9, #1
	movk	x11, #43691
	mul	x9, x9, x11
	and	x11, x9, #0xffff
	cmp	x11, x18
	strh	w9, [x27, :lo12:g_gc_superblock]
	b.eq	.LBB34_185
// %bb.130:                             //   in Loop: Header=BB34_125 Depth=2
	adrp	x12, g_gc_blk_num
	ldrh	w13, [x12, :lo12:g_gc_blk_num]
	cbz	x13, .LBB34_134
// %bb.131:                             //   in Loop: Header=BB34_125 Depth=2
	adrp	x12, p_gc_blk_tbl
	ldr	x12, [x12, :lo12:p_gc_blk_tbl]
	mov	x14, x13
.LBB34_132:                             //   Parent Loop BB34_95 Depth=1
                                        //     Parent Loop BB34_125 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	ldrh	w15, [x12]
	cmp	w15, w9, uxth
	b.eq	.LBB34_147
// %bb.133:                             //   in Loop: Header=BB34_132 Depth=3
	subs	x14, x14, #1                    // =1
	add	x12, x12, #2                    // =2
	b.ne	.LBB34_132
.LBB34_134:                             //   in Loop: Header=BB34_125 Depth=2
	adrp	x12, p_valid_page_count_table
	ldr	x15, [x12, :lo12:p_valid_page_count_table]
	adrp	x12, c_ftl_nand_page_pre_blk
	ldrh	w12, [x12, :lo12:c_ftl_nand_page_pre_blk]
	ldrh	w16, [x17, :lo12:c_ftl_nand_planes_num]
	add	w14, w10, #1                    // =1
	adrp	x10, g_gc_blk_index
	strh	w14, [x10, :lo12:g_gc_blk_index]
	ldrh	w10, [x15, x11, lsl #1]
	mul	w12, w16, w12
	cmp	w12, #0                         // =0
	cinc	w16, w12, lt
	cmp	w10, w16, asr #1
	b.gt	.LBB34_138
// %bb.135:                             //   in Loop: Header=BB34_125 Depth=2
	cmp	w13, #35                        // =35
	b.hi	.LBB34_139
// %bb.136:                             //   in Loop: Header=BB34_125 Depth=2
	and	w13, w14, #0xffff
	cmp	w13, #49                        // =49
	b.lo	.LBB34_139
// %bb.137:                             //   in Loop: Header=BB34_125 Depth=2
	cmp	w10, #9                         // =9
	b.lo	.LBB34_139
.LBB34_138:                             //   in Loop: Header=BB34_125 Depth=2
	adrp	x10, g_gc_blk_index
	strh	wzr, [x10, :lo12:g_gc_blk_index]
	ldrh	w10, [x15, x11, lsl #1]
	mov	w14, wzr
.LBB34_139:                             //   in Loop: Header=BB34_125 Depth=2
	and	w11, w14, #0xffff
	cmp	w11, #3                         // =3
	b.hi	.LBB34_142
// %bb.140:                             //   in Loop: Header=BB34_125 Depth=2
	ldr	x11, [sp, #16]                  // 8-byte Folded Reload
	mvn	w11, w11
	tst	w11, #0xffff
	b.ne	.LBB34_142
// %bb.141:                             //   in Loop: Header=BB34_125 Depth=2
	cmp	w12, w10
	b.le	.LBB34_197
.LBB34_142:                             //   in Loop: Header=BB34_125 Depth=2
	cbnz	w10, .LBB34_175
// %bb.143:                             //   in Loop: Header=BB34_125 Depth=2
	adrp	x9, g_tmp_data_superblock_id
	ldrh	w0, [x9, :lo12:g_tmp_data_superblock_id]
	mov	x19, x17
	cmp	w0, w18
	b.eq	.LBB34_148
// %bb.144:                             //   in Loop: Header=BB34_125 Depth=2
	mov	x20, x9
	bl	update_vpc_list
	adrp	x8, p_data_block_list_head
	adrp	x9, p_data_block_list_table
	ldr	x8, [x8, :lo12:p_data_block_list_head]
	ldr	w9, [x9, :lo12:p_data_block_list_table]
	adrp	x10, p_valid_page_count_table
	ldr	x10, [x10, :lo12:p_valid_page_count_table]
	mov	w11, #43691
	sub	w9, w8, w9
	lsr	w9, w9, #1
	movk	w11, #43690, lsl #16
	mov	w18, #65535
	mul	w9, w9, w11
	strh	w18, [x20, :lo12:g_tmp_data_superblock_id]
	and	x9, x9, #0xffff
	ldrh	w10, [x10, x9, lsl #1]
	cbnz	w10, .LBB34_149
// %bb.145:                             //   in Loop: Header=BB34_125 Depth=2
	cmp	x9, x18
	mov	w20, #6
	b.eq	.LBB34_150
// %bb.146:                             //   in Loop: Header=BB34_125 Depth=2
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.decrement_vpc_count
	mov	w2, #2869
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.decrement_vpc_count
	bl	sftl_printk
	adrp	x8, p_data_block_list_head
	ldr	x8, [x8, :lo12:p_data_block_list_head]
	mov	w18, #65535
	b	.LBB34_150
.LBB34_147:                             //   in Loop: Header=BB34_125 Depth=2
	adrp	x9, g_gc_blk_index
	b	.LBB34_151
.LBB34_148:                             //   in Loop: Header=BB34_125 Depth=2
	strh	w18, [x9, :lo12:g_tmp_data_superblock_id]
	b	.LBB34_150
.LBB34_149:                             //   in Loop: Header=BB34_125 Depth=2
	mov	w20, #6
.LBB34_150:                             //   in Loop: Header=BB34_125 Depth=2
	adrp	x9, g_gc_blk_index
	ldrh	w10, [x9, :lo12:g_gc_blk_index]
	mov	x17, x19
.LBB34_151:                             //   in Loop: Header=BB34_125 Depth=2
	add	w10, w10, #1                    // =1
	strh	w10, [x9, :lo12:g_gc_blk_index]
	cbnz	x8, .LBB34_125
	b	.LBB34_184
.LBB34_152:                             //   in Loop: Header=BB34_95 Depth=1
	ldrh	w8, [x13, :lo12:g_gc_superblock+2]
	ldp	w11, w22, [sp, #8]              // 8-byte Folded Reload
	ldr	w9, [sp, #4]                    // 4-byte Folded Reload
	adrp	x24, g_in_gc_progress
	adrp	x12, g_in_swl_replace
	mov	w20, #6
.LBB34_153:                             //   in Loop: Header=BB34_95 Depth=1
	add	w8, w8, w9
	cmp	w11, w8, uxth
	adrp	x27, g_gc_superblock
	strh	w8, [x13, :lo12:g_gc_superblock+2]
	b.hi	.LBB34_173
// %bb.154:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x8, g_gc_num_req
	ldr	w8, [x8, :lo12:g_gc_num_req]
	mov	x19, x17
	cbz	w8, .LBB34_156
// %bb.155:                             //   in Loop: Header=BB34_95 Depth=1
	mov	x21, x12
	bl	Ftl_gc_temp_data_write_back
	mov	x12, x21
	mov	w18, #65535
	cbnz	w0, .LBB34_204
.LBB34_156:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x8, g_gc_cur_blk_valid_pages
	ldrh	w8, [x8, :lo12:g_gc_cur_blk_valid_pages]
	adrp	x13, g_gc_superblock+2
	cbnz	w8, .LBB34_172
// %bb.157:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x8, p_valid_page_count_table
	ldr	x8, [x8, :lo12:p_valid_page_count_table]
	ldrh	w9, [x27, :lo12:g_gc_superblock]
	ldrh	w8, [x8, x9, lsl #1]
	cbz	w8, .LBB34_172
// %bb.158:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x8, g_MaxLpn
	ldr	w8, [x8, :lo12:g_MaxLpn]
	cbz	w8, .LBB34_165
// %bb.159:                             //   in Loop: Header=BB34_95 Depth=1
	mov	x21, x12
	mov	w26, wzr
	b	.LBB34_161
.LBB34_160:                             //   in Loop: Header=BB34_161 Depth=2
	adrp	x8, g_MaxLpn
	ldr	w8, [x8, :lo12:g_MaxLpn]
	add	w26, w26, #1                    // =1
	cmp	w26, w8
	b.hs	.LBB34_164
.LBB34_161:                             //   Parent Loop BB34_95 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	sub	x1, x29, #16                    // =16
	mov	w0, w26
	mov	w2, wzr
	bl	log2phys
	ldur	w8, [x29, #-16]
	cmn	w8, #1                          // =1
	b.eq	.LBB34_160
// %bb.162:                             //   in Loop: Header=BB34_161 Depth=2
	adrp	x9, c_ftl_nand_blks_per_die
	ldrh	w9, [x9, :lo12:c_ftl_nand_blks_per_die]
	adrp	x10, c_ftl_nand_planes_per_die
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_per_die]
	ubfx	w8, w8, #10, #16
	ldrh	w11, [x27, :lo12:g_gc_superblock]
	udiv	w12, w8, w9
	msub	w8, w12, w9, w8
	udiv	w8, w8, w10
	cmp	w8, w11
	b.ne	.LBB34_160
// %bb.163:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x8, g_MaxLpn
	ldr	w8, [x8, :lo12:g_MaxLpn]
.LBB34_164:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x13, g_gc_superblock+2
	mov	x12, x21
	cmp	w26, w8
	b.lo	.LBB34_171
.LBB34_165:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x8, p_valid_page_count_table
	ldr	x8, [x8, :lo12:p_valid_page_count_table]
	ldrh	w9, [x27, :lo12:g_gc_superblock]
	mov	w26, w22
	strh	wzr, [x8, x9, lsl #1]
	ldrh	w0, [x27, :lo12:g_gc_superblock]
	bl	update_vpc_list
	bl	FtlWriteDump_data
	adrp	x8, c_ftl_nand_l2pmap_ram_region_num
	ldrh	w8, [x8, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	adrp	x11, p_l2p_ram_map
	cbz	w8, .LBB34_170
// %bb.166:                             //   in Loop: Header=BB34_95 Depth=1
	mov	x20, xzr
	mov	x22, xzr
	b	.LBB34_168
.LBB34_167:                             //   in Loop: Header=BB34_168 Depth=2
	add	x22, x22, #1                    // =1
	cmp	x22, w8, uxth
	add	x20, x20, #16                   // =16
	b.hs	.LBB34_170
.LBB34_168:                             //   Parent Loop BB34_95 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldr	x9, [x11, :lo12:p_l2p_ram_map]
	add	x9, x9, x20
	ldr	w10, [x9, #4]
	tbz	w10, #31, .LBB34_167
// %bb.169:                             //   in Loop: Header=BB34_168 Depth=2
	ldrh	w1, [x9]
	ldr	x2, [x9, #8]
	adrp	x0, gL2pMapInfo
	add	x0, x0, :lo12:gL2pMapInfo
	bl	FtlMapWritePage
	adrp	x11, p_l2p_ram_map
	ldr	x8, [x11, :lo12:p_l2p_ram_map]
	add	x8, x8, x20
	ldr	w9, [x8, #4]
	and	w9, w9, #0x7fffffff
	str	w9, [x8, #4]
	adrp	x8, c_ftl_nand_l2pmap_ram_region_num
	ldrh	w8, [x8, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	b	.LBB34_167
.LBB34_170:                             //   in Loop: Header=BB34_95 Depth=1
	bl	FtlVpcTblFlush
	mov	w22, w26
	adrp	x12, g_in_swl_replace
	mov	w20, #6
	adrp	x13, g_gc_superblock+2
.LBB34_171:                             //   in Loop: Header=BB34_95 Depth=1
	mov	w18, #65535
.LBB34_172:                             //   in Loop: Header=BB34_95 Depth=1
	mov	x17, x19
	strh	w18, [x27, :lo12:g_gc_superblock]
.LBB34_173:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x8, g_num_free_superblocks
	ldrh	w8, [x8, :lo12:g_num_free_superblocks]
	str	wzr, [x24, :lo12:g_in_gc_progress]
	cmp	w8, #2                          // =2
	b.hi	.LBB34_198
// %bb.174:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x8, c_ftl_nand_page_pre_blk
	ldrh	w26, [x8, :lo12:c_ftl_nand_page_pre_blk]
	ldrh	w8, [x27, :lo12:g_gc_superblock]
	b	.LBB34_95
.LBB34_175:                             //   in Loop: Header=BB34_95 Depth=1
	mvn	w8, w9
	tst	w8, #0xffff
	adrp	x8, g_gc_superblock+8
	strb	wzr, [x8, :lo12:g_gc_superblock+8]
	b.ne	.LBB34_177
// %bb.176:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.rk_ftl_garbage_collect
	mov	w2, #716
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.rk_ftl_garbage_collect
	mov	x19, x17
	bl	sftl_printk
	ldrh	w9, [x27, :lo12:g_gc_superblock]
	mov	x17, x19
.LBB34_177:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x8, g_active_superblock
	ldrh	w8, [x8, :lo12:g_active_superblock]
	mov	x19, x17
	cmp	w8, w9, uxth
	b.ne	.LBB34_179
// %bb.178:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.rk_ftl_garbage_collect
	mov	w2, #717
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.rk_ftl_garbage_collect
	bl	sftl_printk
	ldrh	w9, [x27, :lo12:g_gc_superblock]
.LBB34_179:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x8, g_buffer_superblock
	ldrh	w8, [x8, :lo12:g_buffer_superblock]
	cmp	w8, w9, uxth
	b.ne	.LBB34_181
// %bb.180:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.rk_ftl_garbage_collect
	mov	w2, #718
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.rk_ftl_garbage_collect
	bl	sftl_printk
	ldrh	w9, [x27, :lo12:g_gc_superblock]
.LBB34_181:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x8, g_gc_temp_superblock
	ldrh	w8, [x8, :lo12:g_gc_temp_superblock]
	cmp	w8, w9, uxth
	b.ne	.LBB34_183
// %bb.182:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.rk_ftl_garbage_collect
	mov	w2, #719
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.rk_ftl_garbage_collect
	bl	sftl_printk
.LBB34_183:                             //   in Loop: Header=BB34_95 Depth=1
	adrp	x21, g_gc_superblock
	add	x21, x21, :lo12:g_gc_superblock
	mov	x0, x21
	bl	make_superblock
	adrp	x8, p_valid_page_count_table
	ldr	x9, [x8, :lo12:p_valid_page_count_table]
	ldrh	w8, [x21]
	adrp	x10, g_gc_cur_blk_valid_pages
	strh	wzr, [x10, :lo12:g_gc_cur_blk_valid_pages]
	adrp	x10, g_gc_cur_blk_max_valid_pages
	ldrh	w9, [x9, x8, lsl #1]
	mov	x17, x19
	adrp	x13, g_gc_superblock+2
	mov	w18, #65535
	mov	x12, x25
	strh	wzr, [x21, #2]
	strh	w9, [x10, :lo12:g_gc_cur_blk_max_valid_pages]
	strb	wzr, [x21, #6]
	b	.LBB34_96
.LBB34_184:
	mov	w8, #65535
	strh	w8, [x27, :lo12:g_gc_superblock]
.LBB34_185:
	adrp	x8, g_gc_blk_index
	strh	wzr, [x8, :lo12:g_gc_blk_index]
	mov	w20, #8
	b	.LBB34_187
.LBB34_186:
	adrp	x8, gc_ink_free_return_value
	ldrh	w20, [x8, :lo12:gc_ink_free_return_value]
	adrp	x8, g_in_gc_progress
	str	wzr, [x8, :lo12:g_in_gc_progress]
	mov	w8, #65535
	adrp	x9, g_gc_superblock
	str	w8, [x9, :lo12:g_gc_superblock]
.LBB34_187:
	adrp	x9, __stack_chk_guard
	ldur	x8, [x29, #-8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB34_205
// %bb.188:
	mov	w0, w20
	ldp	x20, x19, [sp, #128]            // 16-byte Folded Reload
	ldp	x22, x21, [sp, #112]            // 16-byte Folded Reload
	ldp	x24, x23, [sp, #96]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #80]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #64]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #48]             // 16-byte Folded Reload
	add	sp, sp, #144                    // =144
	hint	#29
	ret
.LBB34_189:
	mov	w8, #18
	b	.LBB34_45
.LBB34_190:
	adrp	x19, g_tmp_data_superblock_id
	ldrh	w0, [x19, :lo12:g_tmp_data_superblock_id]
	mov	w20, #65535
	cmp	w0, w20
	b.eq	.LBB34_201
// %bb.191:
	bl	update_vpc_list
	adrp	x9, p_data_block_list_table
	ldr	w8, [x22, :lo12:p_data_block_list_head]
	ldr	w9, [x9, :lo12:p_data_block_list_table]
	ldr	x10, [x21, :lo12:p_valid_page_count_table]
	strh	w20, [x19, :lo12:g_tmp_data_superblock_id]
	sub	w8, w8, w9
	mov	w9, #43691
	lsr	w8, w8, #1
	movk	w9, #43690, lsl #16
	mul	w8, w8, w9
	and	x8, x8, #0xffff
	ldrh	w9, [x10, x8, lsl #1]
	cbnz	w9, .LBB34_194
// %bb.192:
	mov	w9, #65535
	cmp	x8, x9
	b.eq	.LBB34_194
// %bb.193:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.decrement_vpc_count
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.decrement_vpc_count
	mov	w2, #2869
	bl	sftl_printk
.LBB34_194:
	ldrh	w8, [x25, :lo12:g_num_free_superblocks]
	b	.LBB34_202
.LBB34_195:
	mov	w9, #18
	b	.LBB34_59
.LBB34_196:
	bl	FtlReadRefresh
	ldrh	w20, [x20, :lo12:gc_ink_free_return_value]
	b	.LBB34_187
.LBB34_197:
	adrp	x8, gc_ink_free_return_value
	ldrh	w20, [x8, :lo12:gc_ink_free_return_value]
	mov	w8, #65535
	strh	w8, [x27, :lo12:g_gc_superblock]
	adrp	x8, g_gc_blk_index
	strh	wzr, [x8, :lo12:g_gc_blk_index]
	b	.LBB34_187
.LBB34_198:
	adrp	x9, gc_ink_free_return_value
	ldrh	w20, [x9, :lo12:gc_ink_free_return_value]
	cbnz	w20, .LBB34_187
	b	.LBB34_202
.LBB34_199:
	mov	w28, w14
	b	.LBB34_24
.LBB34_200:
	strh	w21, [x19, :lo12:g_gc_next_blk]
	b	.LBB34_34
.LBB34_201:
	ldrh	w8, [x25, :lo12:g_num_free_superblocks]
	strh	w20, [x19, :lo12:g_tmp_data_superblock_id]
.LBB34_202:
	add	w20, w8, #1                     // =1
	b	.LBB34_187
.LBB34_203:
	mov	w9, #64
	b	.LBB34_22
.LBB34_204:
	adrp	x8, gc_ink_free_return_value
	ldrh	w20, [x8, :lo12:gc_ink_free_return_value]
	str	wzr, [x24, :lo12:g_in_gc_progress]
	b	.LBB34_187
.LBB34_205:
	bl	__stack_chk_fail
.Lfunc_end34:
	.size	rk_ftl_garbage_collect, .Lfunc_end34-rk_ftl_garbage_collect
                                        // -- End function
	.globl	FtlSysFlush                     // -- Begin function FtlSysFlush
	.p2align	2
	.type	FtlSysFlush,@function
FtlSysFlush:                            // @FtlSysFlush
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-64]!           // 16-byte Folded Spill
	str	x23, [sp, #16]                  // 8-byte Folded Spill
	stp	x22, x21, [sp, #32]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #48]             // 16-byte Folded Spill
	mov	x29, sp
	bl	FtlWriteDump_data
	adrp	x20, c_ftl_nand_l2pmap_ram_region_num
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	cbz	w8, .LBB35_5
// %bb.1:
	adrp	x19, gL2pMapInfo
	mov	x21, xzr
	mov	x22, xzr
	adrp	x23, p_l2p_ram_map
	add	x19, x19, :lo12:gL2pMapInfo
	b	.LBB35_3
.LBB35_2:                               //   in Loop: Header=BB35_3 Depth=1
	add	x22, x22, #1                    // =1
	cmp	x22, w8, uxth
	add	x21, x21, #16                   // =16
	b.hs	.LBB35_5
.LBB35_3:                               // =>This Inner Loop Header: Depth=1
	ldr	x9, [x23, :lo12:p_l2p_ram_map]
	add	x9, x9, x21
	ldr	w10, [x9, #4]
	tbz	w10, #31, .LBB35_2
// %bb.4:                               //   in Loop: Header=BB35_3 Depth=1
	ldrh	w1, [x9]
	ldr	x2, [x9, #8]
	mov	x0, x19
	bl	FtlMapWritePage
	ldr	x8, [x23, :lo12:p_l2p_ram_map]
	add	x8, x8, x21
	ldr	w9, [x8, #4]
	and	w9, w9, #0x7fffffff
	str	w9, [x8, #4]
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	b	.LBB35_2
.LBB35_5:
	mov	w0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	ldp	x20, x19, [sp, #48]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #32]             // 16-byte Folded Reload
	ldr	x23, [sp, #16]                  // 8-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #64             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end35:
	.size	FtlSysFlush, .Lfunc_end35-FtlSysFlush
                                        // -- End function
	.globl	FtlCacheWriteBack               // -- Begin function FtlCacheWriteBack
	.p2align	2
	.type	FtlCacheWriteBack,@function
FtlCacheWriteBack:                      // @FtlCacheWriteBack
// %bb.0:
	hint	#25
	mov	w0, wzr
	hint	#29
	ret
.Lfunc_end36:
	.size	FtlCacheWriteBack, .Lfunc_end36-FtlCacheWriteBack
                                        // -- End function
	.globl	l2p_flush                       // -- Begin function l2p_flush
	.p2align	2
	.type	l2p_flush,@function
l2p_flush:                              // @l2p_flush
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-64]!           // 16-byte Folded Spill
	str	x23, [sp, #16]                  // 8-byte Folded Spill
	stp	x22, x21, [sp, #32]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #48]             // 16-byte Folded Spill
	mov	x29, sp
	bl	FtlWriteDump_data
	adrp	x20, c_ftl_nand_l2pmap_ram_region_num
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	cbz	w8, .LBB37_5
// %bb.1:
	adrp	x19, gL2pMapInfo
	mov	x21, xzr
	mov	x22, xzr
	adrp	x23, p_l2p_ram_map
	add	x19, x19, :lo12:gL2pMapInfo
	b	.LBB37_3
.LBB37_2:                               //   in Loop: Header=BB37_3 Depth=1
	add	x22, x22, #1                    // =1
	cmp	x22, w8, uxth
	add	x21, x21, #16                   // =16
	b.hs	.LBB37_5
.LBB37_3:                               // =>This Inner Loop Header: Depth=1
	ldr	x9, [x23, :lo12:p_l2p_ram_map]
	add	x9, x9, x21
	ldr	w10, [x9, #4]
	tbz	w10, #31, .LBB37_2
// %bb.4:                               //   in Loop: Header=BB37_3 Depth=1
	ldrh	w1, [x9]
	ldr	x2, [x9, #8]
	mov	x0, x19
	bl	FtlMapWritePage
	ldr	x8, [x23, :lo12:p_l2p_ram_map]
	add	x8, x8, x21
	ldr	w9, [x8, #4]
	and	w9, w9, #0x7fffffff
	str	w9, [x8, #4]
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	b	.LBB37_2
.LBB37_5:
	ldp	x20, x19, [sp, #48]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #32]             // 16-byte Folded Reload
	ldr	x23, [sp, #16]                  // 8-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #64             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end37:
	.size	l2p_flush, .Lfunc_end37-l2p_flush
                                        // -- End function
	.globl	FtlEctTblFlush                  // -- Begin function FtlEctTblFlush
	.p2align	2
	.type	FtlEctTblFlush,@function
FtlEctTblFlush:                         // @FtlEctTblFlush
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	adrp	x8, g_ect_tbl_power_up_flush
	ldrh	w9, [x8, :lo12:g_ect_tbl_power_up_flush]
	mov	x29, sp
	cmp	w9, #31                         // =31
	b.hi	.LBB38_2
// %bb.1:
	add	w9, w9, #1                      // =1
	strh	w9, [x8, :lo12:g_ect_tbl_power_up_flush]
	mov	w9, #1
	b	.LBB38_3
.LBB38_2:
	mov	w9, #32
.LBB38_3:
	adrp	x8, gp_ect_tbl_info
	ldr	x10, [x8, :lo12:gp_ect_tbl_info]
	ldr	w11, [x10, #16]
	cbnz	w0, .LBB38_5
// %bb.4:
	ldr	w12, [x10, #20]
	add	w9, w12, w9
	cmp	w11, w9
	b.lo	.LBB38_7
.LBB38_5:
	mov	w9, #17221
	movk	w9, #16980, lsl #16
	str	w11, [x10, #20]
	str	w9, [x10]
	ldr	x9, [x8, :lo12:gp_ect_tbl_info]
	adrp	x10, g_ect_tbl_info_size
	ldrh	w1, [x10, :lo12:g_ect_tbl_info_size]
	mov	w0, #64
	ldr	w10, [x9, #8]
	str	wzr, [x9, #4]
	lsl	w11, w1, #9
	add	w10, w10, #1                    // =1
	stp	w10, w11, [x9, #8]
	ldr	x2, [x8, :lo12:gp_ect_tbl_info]
	bl	FtlVendorPartWrite
	adrp	x8, g_sys_ext_data
	ldr	w8, [x8, :lo12:g_sys_ext_data]
	mov	w9, #19539
	movk	w9, #18004, lsl #16
	cmp	w8, w9
	b.ne	.LBB38_7
// %bb.6:
	adrp	x9, g_totle_write_sector
	adrp	x10, g_totle_read_sector
	ldr	w9, [x9, :lo12:g_totle_write_sector]
	ldr	w10, [x10, :lo12:g_totle_read_sector]
	adrp	x2, g_sys_ext_data
	add	x2, x2, :lo12:g_sys_ext_data
	adrp	x11, g_totle_gc_page_count
	adrp	x12, g_totle_write_page_count
	adrp	x13, g_totle_read_page_count
	stp	w9, w10, [x2, #88]
	ldr	w10, [x11, :lo12:g_totle_gc_page_count]
	ldr	w11, [x12, :lo12:g_totle_write_page_count]
	ldr	w12, [x13, :lo12:g_totle_read_page_count]
	mov	w8, #87
	movk	w8, #20480, lsl #16
	adrp	x14, g_totle_l2p_write_count
	adrp	x15, g_totle_slc_erase_count
	adrp	x9, g_totle_sys_slc_erase_count
	stp	w8, w10, [x2, #4]
	ldr	w10, [x14, :lo12:g_totle_l2p_write_count]
	stp	w11, w12, [x2, #12]
	ldr	w12, [x15, :lo12:g_totle_slc_erase_count]
	ldr	w9, [x9, :lo12:g_totle_sys_slc_erase_count]
	adrp	x8, g_totle_discard_page_count
	adrp	x11, g_totle_cache_write_count
	str	w10, [x2, #20]
	adrp	x10, g_max_erase_count
	stp	w12, w9, [x2, #28]
	adrp	x9, g_min_erase_count
	ldr	w8, [x8, :lo12:g_totle_discard_page_count]
	ldr	w11, [x11, :lo12:g_totle_cache_write_count]
	ldr	w10, [x10, :lo12:g_max_erase_count]
	ldr	w9, [x9, :lo12:g_min_erase_count]
	mov	w1, #1
	mov	w0, wzr
	stp	w8, w11, [x2, #36]
	stp	w10, w9, [x2, #44]
	bl	FtlVendorPartWrite
.LBB38_7:
	mov	w0, wzr
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end38:
	.size	FtlEctTblFlush, .Lfunc_end38-FtlEctTblFlush
                                        // -- End function
	.globl	sftl_deinit                     // -- Begin function sftl_deinit
	.p2align	2
	.type	sftl_deinit,@function
sftl_deinit:                            // @sftl_deinit
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-64]!           // 16-byte Folded Spill
	adrp	x8, gFtlInitStatus
	ldr	w8, [x8, :lo12:gFtlInitStatus]
	str	x23, [sp, #16]                  // 8-byte Folded Spill
	stp	x22, x21, [sp, #32]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #48]             // 16-byte Folded Spill
	cmp	w8, #1                          // =1
	mov	x29, sp
	b.ne	.LBB39_7
// %bb.1:
	bl	FtlWriteDump_data
	adrp	x20, c_ftl_nand_l2pmap_ram_region_num
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	cbz	w8, .LBB39_6
// %bb.2:
	adrp	x19, gL2pMapInfo
	mov	x21, xzr
	mov	x22, xzr
	adrp	x23, p_l2p_ram_map
	add	x19, x19, :lo12:gL2pMapInfo
	b	.LBB39_4
.LBB39_3:                               //   in Loop: Header=BB39_4 Depth=1
	add	x22, x22, #1                    // =1
	cmp	x22, w8, uxth
	add	x21, x21, #16                   // =16
	b.hs	.LBB39_6
.LBB39_4:                               // =>This Inner Loop Header: Depth=1
	ldr	x9, [x23, :lo12:p_l2p_ram_map]
	add	x9, x9, x21
	ldr	w10, [x9, #4]
	tbz	w10, #31, .LBB39_3
// %bb.5:                               //   in Loop: Header=BB39_4 Depth=1
	ldrh	w1, [x9]
	ldr	x2, [x9, #8]
	mov	x0, x19
	bl	FtlMapWritePage
	ldr	x8, [x23, :lo12:p_l2p_ram_map]
	add	x8, x8, x21
	ldr	w9, [x8, #4]
	and	w9, w9, #0x7fffffff
	str	w9, [x8, #4]
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	b	.LBB39_3
.LBB39_6:
	mov	w0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.LBB39_7:
	ldp	x20, x19, [sp, #48]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #32]             // 16-byte Folded Reload
	ldr	x23, [sp, #16]                  // 8-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #64             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end39:
	.size	sftl_deinit, .Lfunc_end39-sftl_deinit
                                        // -- End function
	.globl	FtlRead                         // -- Begin function FtlRead
	.p2align	2
	.type	FtlRead,@function
FtlRead:                                // @FtlRead
// %bb.0:
	hint	#25
	sub	sp, sp, #176                    // =176
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	and	w9, w0, #0xff
	stp	x29, x30, [sp, #80]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #144]            // 16-byte Folded Spill
	stp	x20, x19, [sp, #160]            // 16-byte Folded Spill
	add	x29, sp, #80                    // =80
	mov	x19, x3
	mov	w21, w1
	cmp	w9, #16                         // =16
	stp	x28, x27, [sp, #96]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #112]            // 16-byte Folded Spill
	stp	x24, x23, [sp, #128]            // 16-byte Folded Spill
	stur	x8, [x29, #-8]
	b.ne	.LBB40_2
// %bb.1:
	add	w0, w21, #256                   // =256
	mov	w1, w2
	mov	x2, x19
	bl	FtlVendorPartRead
	b	.LBB40_12
.LBB40_2:
	adrp	x8, g_MaxLbaSector
	ldr	w8, [x8, :lo12:g_MaxLbaSector]
	mov	w0, #-1
	cmp	w8, w21
	b.ls	.LBB40_12
// %bb.3:
	cmp	w8, w2
	b.lo	.LBB40_12
// %bb.4:
	add	w9, w2, w21
	cmp	w9, w8
	mov	w0, #-1
	stur	w9, [x29, #-16]                 // 4-byte Folded Spill
	b.hi	.LBB40_12
// %bb.5:
	adrp	x8, gFtlInitStatus
	ldr	w8, [x8, :lo12:gFtlInitStatus]
	cmn	w8, #1                          // =1
	b.eq	.LBB40_12
// %bb.6:
	adrp	x10, c_ftl_nand_sec_pre_page
	ldur	w8, [x29, #-16]                 // 4-byte Folded Reload
	ldrh	w10, [x10, :lo12:c_ftl_nand_sec_pre_page]
	adrp	x9, g_totle_read_sector
	adrp	x11, g_totle_read_page_count
	ldr	w12, [x9, :lo12:g_totle_read_sector]
	sub	w8, w8, #1                      // =1
	ldr	w13, [x11, :lo12:g_totle_read_page_count]
	udiv	w24, w21, w10
	udiv	w23, w8, w10
	sub	w10, w23, w24
	add	w8, w12, w2
	adds	w25, w10, #1                    // =1
	str	w8, [x9, :lo12:g_totle_read_sector]
	add	w8, w13, w25
	str	w8, [x11, :lo12:g_totle_read_page_count]
	b.lo	.LBB40_14
// %bb.7:
	stur	wzr, [x29, #-20]                // 4-byte Folded Spill
.LBB40_8:
	adrp	x8, g_gc_bad_block_temp_num
	ldrh	w8, [x8, :lo12:g_gc_bad_block_temp_num]
	cbnz	w8, .LBB40_10
// %bb.9:
	adrp	x8, g_num_free_superblocks
	ldrh	w8, [x8, :lo12:g_num_free_superblocks]
	cmp	w8, #31                         // =31
	b.hi	.LBB40_11
.LBB40_10:
	mov	w0, wzr
	bl	rk_ftl_garbage_collect
.LBB40_11:
	ldur	w0, [x29, #-20]                 // 4-byte Folded Reload
.LBB40_12:
	adrp	x9, __stack_chk_guard
	ldur	x8, [x29, #-8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB40_56
// %bb.13:
	ldp	x20, x19, [sp, #160]            // 16-byte Folded Reload
	ldp	x22, x21, [sp, #144]            // 16-byte Folded Reload
	ldp	x24, x23, [sp, #128]            // 16-byte Folded Reload
	ldp	x26, x25, [sp, #112]            // 16-byte Folded Reload
	ldp	x28, x27, [sp, #96]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #80]             // 16-byte Folded Reload
	add	sp, sp, #176                    // =176
	hint	#29
	ret
.LBB40_14:
	mov	w27, wzr
	adrp	x26, req_read
	adrp	x28, g_sys_ext_data+72
	mov	w20, w24
	str	w2, [sp, #12]                   // 4-byte Folded Spill
	str	xzr, [sp, #16]                  // 8-byte Folded Spill
	str	wzr, [sp, #24]                  // 4-byte Folded Spill
	stur	wzr, [x29, #-20]                // 4-byte Folded Spill
	b	.LBB40_17
.LBB40_15:                              //   in Loop: Header=BB40_17 Depth=1
	ldr	w25, [sp, #28]                  // 4-byte Folded Reload
	mov	w27, wzr
.LBB40_16:                              //   in Loop: Header=BB40_17 Depth=1
	add	w20, w20, #1                    // =1
	cbz	w25, .LBB40_8
.LBB40_17:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB40_24 Depth 2
                                        //     Child Loop BB40_39 Depth 2
	sub	x1, x29, #12                    // =12
	mov	w0, w20
	mov	w2, wzr
	bl	log2phys
	ldur	w9, [x29, #-12]
	cmn	w9, #1                          // =1
	b.eq	.LBB40_21
// %bb.18:                              //   in Loop: Header=BB40_17 Depth=1
	ldr	x10, [x26, :lo12:req_read]
	mov	w8, w27
	cmp	w20, w24
	add	x10, x10, x8, lsl #5
	str	w9, [x10, #4]
	b.ne	.LBB40_27
// %bb.19:                              //   in Loop: Header=BB40_17 Depth=1
	ldr	x9, [x26, :lo12:req_read]
	adrp	x10, p_io_data_buf_0
	ldr	x10, [x10, :lo12:p_io_data_buf_0]
	ldr	w11, [sp, #12]                  // 4-byte Folded Reload
	add	x9, x9, x8, lsl #5
	str	x10, [x9, #8]
	adrp	x9, c_ftl_nand_sec_pre_page
	ldrh	w9, [x9, :lo12:c_ftl_nand_sec_pre_page]
	udiv	w10, w21, w9
	msub	w10, w10, w9, w21
	str	w10, [sp, #20]                  // 4-byte Folded Spill
	sub	w10, w9, w10
	cmp	w10, w11
	csel	w10, w11, w10, hi
	cmp	w10, w9
	str	w10, [sp, #24]                  // 4-byte Folded Spill
	b.ne	.LBB40_33
// %bb.20:                              //   in Loop: Header=BB40_17 Depth=1
	ldr	x10, [x26, :lo12:req_read]
	str	w9, [sp, #24]                   // 4-byte Folded Spill
	add	x10, x10, x8, lsl #5
	str	x19, [x10, #8]
	b	.LBB40_33
.LBB40_21:                              //   in Loop: Header=BB40_17 Depth=1
	adrp	x8, c_ftl_nand_sec_pre_page
	ldrh	w22, [x8, :lo12:c_ftl_nand_sec_pre_page]
	cbz	w22, .LBB40_31
// %bb.22:                              //   in Loop: Header=BB40_17 Depth=1
	stur	w27, [x29, #-32]                // 4-byte Folded Spill
	str	w25, [sp, #28]                  // 4-byte Folded Spill
	mov	x25, x19
	mov	w27, wzr
	mul	w19, w20, w22
	b	.LBB40_24
.LBB40_23:                              //   in Loop: Header=BB40_24 Depth=2
	add	w27, w27, #1                    // =1
	cmp	w27, w22
	b.hs	.LBB40_30
.LBB40_24:                              //   Parent Loop BB40_17 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	add	w9, w19, w27
	subs	w8, w9, w21
	b.lo	.LBB40_23
// %bb.25:                              //   in Loop: Header=BB40_24 Depth=2
	ldur	w10, [x29, #-16]                // 4-byte Folded Reload
	cmp	w9, w10
	b.hs	.LBB40_23
// %bb.26:                              //   in Loop: Header=BB40_24 Depth=2
	lsl	w8, w8, #9
	add	x0, x25, x8
	mov	w2, #512
	mov	w1, wzr
	bl	memset
	b	.LBB40_23
.LBB40_27:                              //   in Loop: Header=BB40_17 Depth=1
	cmp	w20, w23
	b.ne	.LBB40_32
// %bb.28:                              //   in Loop: Header=BB40_17 Depth=1
	ldr	x9, [x26, :lo12:req_read]
	adrp	x10, p_io_data_buf_1
	ldr	x10, [x10, :lo12:p_io_data_buf_1]
	ldur	w11, [x29, #-16]                // 4-byte Folded Reload
	add	x9, x9, x8, lsl #5
	str	x10, [x9, #8]
	adrp	x9, c_ftl_nand_sec_pre_page
	ldrh	w9, [x9, :lo12:c_ftl_nand_sec_pre_page]
	mul	w10, w23, w9
	sub	w11, w11, w10
	cmp	w11, w9
	str	w11, [sp, #16]                  // 4-byte Folded Spill
	b.ne	.LBB40_33
// %bb.29:                              //   in Loop: Header=BB40_17 Depth=1
	ldr	x11, [x26, :lo12:req_read]
	sub	w10, w10, w21
	lsl	w10, w10, #9
	add	x10, x19, x10
	add	x11, x11, x8, lsl #5
	str	x10, [x11, #8]
	str	w9, [sp, #16]                   // 4-byte Folded Spill
	b	.LBB40_33
.LBB40_30:                              //   in Loop: Header=BB40_17 Depth=1
	mov	x19, x25
	ldr	w25, [sp, #28]                  // 4-byte Folded Reload
	ldur	w27, [x29, #-32]                // 4-byte Folded Reload
.LBB40_31:                              //   in Loop: Header=BB40_17 Depth=1
	subs	w25, w25, #1                    // =1
	b.ne	.LBB40_34
	b	.LBB40_35
.LBB40_32:                              //   in Loop: Header=BB40_17 Depth=1
	adrp	x9, c_ftl_nand_sec_pre_page
	ldrh	w9, [x9, :lo12:c_ftl_nand_sec_pre_page]
	ldr	x11, [x26, :lo12:req_read]
	neg	w10, w21
	madd	w9, w20, w9, w10
	lsl	w9, w9, #9
	add	x9, x19, x9
	add	x10, x11, x8, lsl #5
	str	x9, [x10, #8]
.LBB40_33:                              //   in Loop: Header=BB40_17 Depth=1
	adrp	x9, c_ftl_nand_byte_pre_oob
	ldrh	w9, [x9, :lo12:c_ftl_nand_byte_pre_oob]
	adrp	x11, p_io_spare_buf
	ldr	x10, [x26, :lo12:req_read]
	ldr	x11, [x11, :lo12:p_io_spare_buf]
	mul	w9, w27, w9
	lsl	x8, x8, #5
	and	w9, w9, #0xfffffffc
	add	x10, x10, x8
	add	x9, x11, x9
	str	x9, [x10, #16]
	ldr	x9, [x26, :lo12:req_read]
	add	w27, w27, #1                    // =1
	add	x8, x9, x8
	str	w20, [x8, #24]
	subs	w25, w25, #1                    // =1
	b.eq	.LBB40_35
.LBB40_34:                              //   in Loop: Header=BB40_17 Depth=1
	adrp	x8, c_ftl_nand_planes_num
	ldrh	w8, [x8, :lo12:c_ftl_nand_planes_num]
	cmp	w27, w8, lsl #2
	b.ne	.LBB40_16
.LBB40_35:                              //   in Loop: Header=BB40_17 Depth=1
	cbz	w27, .LBB40_16
// %bb.36:                              //   in Loop: Header=BB40_17 Depth=1
	ldr	x0, [x26, :lo12:req_read]
	mov	w1, w27
	str	w25, [sp, #28]                  // 4-byte Folded Spill
	bl	FlashReadPages
	ldr	w8, [sp, #16]                   // 4-byte Folded Reload
	mov	x22, xzr
	lsl	w8, w8, #9
	str	x8, [sp, #32]                   // 8-byte Folded Spill
	ldr	w8, [sp, #20]                   // 4-byte Folded Reload
	lsl	w8, w8, #9
	stur	x8, [x29, #-32]                 // 8-byte Folded Spill
	ldr	w8, [sp, #24]                   // 4-byte Folded Reload
	lsl	w8, w8, #9
	str	x8, [sp, #40]                   // 8-byte Folded Spill
	mov	w8, w27
	lsl	x27, x8, #5
	b	.LBB40_39
.LBB40_37:                              //   in Loop: Header=BB40_39 Depth=2
	adrp	x8, g_gc_next_blk
	strh	w25, [x8, :lo12:g_gc_next_blk]
.LBB40_38:                              //   in Loop: Header=BB40_39 Depth=2
	add	x22, x22, #32                   // =32
	cmp	x27, x22
	b.eq	.LBB40_15
.LBB40_39:                              //   Parent Loop BB40_17 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldr	x8, [x26, :lo12:req_read]
	add	x9, x8, x22
	ldr	w10, [x9, #24]
	cmp	w10, w24
	b.ne	.LBB40_42
// %bb.40:                              //   in Loop: Header=BB40_39 Depth=2
	ldr	x8, [x9, #8]
	adrp	x9, p_io_data_buf_0
	ldr	x9, [x9, :lo12:p_io_data_buf_0]
	cmp	x8, x9
	b.ne	.LBB40_46
// %bb.41:                              //   in Loop: Header=BB40_39 Depth=2
	ldur	x9, [x29, #-32]                 // 8-byte Folded Reload
	ldr	x2, [sp, #40]                   // 8-byte Folded Reload
	mov	x0, x19
	add	x1, x8, x9
	b	.LBB40_45
.LBB40_42:                              //   in Loop: Header=BB40_39 Depth=2
	cmp	w10, w23
	b.ne	.LBB40_46
// %bb.43:                              //   in Loop: Header=BB40_39 Depth=2
	add	x8, x8, x22
	ldr	x1, [x8, #8]
	adrp	x8, p_io_data_buf_1
	ldr	x8, [x8, :lo12:p_io_data_buf_1]
	cmp	x1, x8
	b.ne	.LBB40_46
// %bb.44:                              //   in Loop: Header=BB40_39 Depth=2
	adrp	x8, c_ftl_nand_sec_pre_page
	ldrh	w8, [x8, :lo12:c_ftl_nand_sec_pre_page]
	neg	w9, w21
	ldr	x2, [sp, #32]                   // 8-byte Folded Reload
	madd	w8, w23, w8, w9
	lsl	w8, w8, #9
	add	x0, x19, x8
.LBB40_45:                              //   in Loop: Header=BB40_39 Depth=2
	bl	memcpy
.LBB40_46:                              //   in Loop: Header=BB40_39 Depth=2
	ldr	x8, [x26, :lo12:req_read]
	add	x9, x8, x22
	ldr	x10, [x9, #16]
	ldr	w9, [x9, #24]
	ldr	w10, [x10, #8]
	cmp	w9, w10
	b.eq	.LBB40_48
// %bb.47:                              //   in Loop: Header=BB40_39 Depth=2
	ldr	w9, [x28, :lo12:g_sys_ext_data+72]
	add	w9, w9, #1                      // =1
	str	w9, [x28, :lo12:g_sys_ext_data+72]
.LBB40_48:                              //   in Loop: Header=BB40_39 Depth=2
	ldr	w9, [x8, x22]
	cmn	w9, #1                          // =1
	b.eq	.LBB40_50
// %bb.49:                              //   in Loop: Header=BB40_39 Depth=2
	cmp	w9, #256                        // =256
	b.ne	.LBB40_38
	b	.LBB40_51
.LBB40_50:                              //   in Loop: Header=BB40_39 Depth=2
	ldr	w9, [x28, :lo12:g_sys_ext_data+72]
	mov	w10, #-1
	stur	w10, [x29, #-20]                // 4-byte Folded Spill
	add	w9, w9, #1                      // =1
	str	w9, [x28, :lo12:g_sys_ext_data+72]
	ldr	w9, [x8, x22]
	cmp	w9, #256                        // =256
	b.ne	.LBB40_38
.LBB40_51:                              //   in Loop: Header=BB40_39 Depth=2
	add	x8, x8, x22
	ldr	w8, [x8, #4]
	adrp	x9, c_ftl_nand_blks_per_die
	ldrh	w9, [x9, :lo12:c_ftl_nand_blks_per_die]
	adrp	x10, c_ftl_nand_planes_per_die
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_per_die]
	ubfx	w8, w8, #10, #16
	udiv	w11, w8, w9
	msub	w8, w11, w9, w8
	udiv	w25, w8, w10
	adrp	x0, .L.str.112
	add	x0, x0, :lo12:.L.str.112
	mov	w1, w25
	bl	sftl_printk
	adrp	x8, g_gc_next_blk
	ldrh	w8, [x8, :lo12:g_gc_next_blk]
	cmp	w8, w25
	b.eq	.LBB40_38
// %bb.52:                              //   in Loop: Header=BB40_39 Depth=2
	adrp	x9, g_gc_next_blk_1
	ldrh	w9, [x9, :lo12:g_gc_next_blk_1]
	cmp	w9, w25, uxth
	b.eq	.LBB40_38
// %bb.53:                              //   in Loop: Header=BB40_39 Depth=2
	mov	w10, #65535
	cmp	w8, w10
	mov	w8, #65535
	b.eq	.LBB40_37
// %bb.54:                              //   in Loop: Header=BB40_39 Depth=2
	cmp	w9, w8
	b.ne	.LBB40_38
// %bb.55:                              //   in Loop: Header=BB40_39 Depth=2
	adrp	x8, g_gc_next_blk_1
	strh	w25, [x8, :lo12:g_gc_next_blk_1]
	b	.LBB40_38
.LBB40_56:
	bl	__stack_chk_fail
.Lfunc_end40:
	.size	FtlRead, .Lfunc_end40-FtlRead
                                        // -- End function
	.globl	FtlVendorPartRead               // -- Begin function FtlVendorPartRead
	.p2align	2
	.type	FtlVendorPartRead,@function
FtlVendorPartRead:                      // @FtlVendorPartRead
// %bb.0:
	hint	#25
	sub	sp, sp, #208                    // =208
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	stp	x29, x30, [sp, #112]            // 16-byte Folded Spill
	add	x29, sp, #112                   // =112
	stp	x28, x27, [sp, #128]            // 16-byte Folded Spill
	stp	x26, x25, [sp, #144]            // 16-byte Folded Spill
	stp	x24, x23, [sp, #160]            // 16-byte Folded Spill
	stp	x22, x21, [sp, #176]            // 16-byte Folded Spill
	stp	x20, x19, [sp, #192]            // 16-byte Folded Spill
	stur	x8, [x29, #-8]
	adrp	x8, c_ftl_vendor_part_size
	ldrh	w8, [x8, :lo12:c_ftl_vendor_part_size]
	add	w9, w1, w0
	cmp	w9, w8
	b.ls	.LBB41_2
// %bb.1:
	mov	w19, #-1
	b	.LBB41_11
.LBB41_2:
	mov	w20, w1
	cbz	w1, .LBB41_10
// %bb.3:
	adrp	x8, c_ftl_nand_sec_pre_page_shift
	ldrh	w8, [x8, :lo12:c_ftl_nand_sec_pre_page_shift]
	mov	w21, w0
	mov	x22, x2
	mov	w19, wzr
	adrp	x28, c_ftl_nand_sec_pre_page
	adrp	x23, p_vendor_data_buf
	lsr	w24, w0, w8
	b	.LBB41_6
.LBB41_4:                               //   in Loop: Header=BB41_6 Depth=1
	ldr	x8, [x23, :lo12:p_vendor_data_buf]
	mov	w10, w25
	lsl	w9, w27, #7
	and	x10, x10, #0xffff
	add	x1, x8, w9, uxtw #2
	lsl	x2, x10, #9
	mov	x0, x22
	bl	memcpy
.LBB41_5:                               //   in Loop: Header=BB41_6 Depth=1
	and	w8, w25, #0xffff
	subs	w20, w20, w8
	add	w21, w21, w8
	and	x8, x25, #0xffff
	add	w24, w24, #1                    // =1
	add	x22, x22, x8, lsl #9
	b.eq	.LBB41_11
.LBB41_6:                               // =>This Inner Loop Header: Depth=1
	adrp	x8, p_vendor_region_ppn_table
	ldr	x8, [x8, :lo12:p_vendor_region_ppn_table]
	ldrh	w9, [x28, :lo12:c_ftl_nand_sec_pre_page]
	ldr	w26, [x8, w24, uxtw #2]
	udiv	w8, w21, w9
	msub	w27, w8, w9, w21
	sub	w8, w9, w27
	cmp	w20, w8, uxth
	csel	w25, w20, w8, lo
	cbz	w26, .LBB41_9
// %bb.7:                               //   in Loop: Header=BB41_6 Depth=1
	ldr	x9, [x23, :lo12:p_vendor_data_buf]
	add	x8, sp, #40                     // =40
	add	x0, sp, #8                      // =8
	mov	w1, #1
	str	w26, [sp, #12]
	stp	x9, x8, [sp, #16]
	bl	FlashReadPages
	ldr	w8, [sp, #8]
	adrp	x9, req_sys
	ldr	w9, [x9, :lo12:req_sys]
	cmn	w8, #1                          // =1
	csinv	w19, w19, wzr, ne
	cmp	w9, #256                        // =256
	b.ne	.LBB41_4
// %bb.8:                               //   in Loop: Header=BB41_6 Depth=1
	adrp	x0, .L.str.94
	add	x0, x0, :lo12:.L.str.94
	mov	w1, w24
	mov	w2, w26
	bl	sftl_printk
	ldr	x2, [x23, :lo12:p_vendor_data_buf]
	adrp	x0, gVendorBlkInfo
	add	x0, x0, :lo12:gVendorBlkInfo
	mov	w1, w24
	bl	FtlMapWritePage
	b	.LBB41_4
.LBB41_9:                               //   in Loop: Header=BB41_6 Depth=1
	mov	w8, w25
	and	x8, x8, #0xffff
	lsl	x2, x8, #9
	mov	x0, x22
	mov	w1, wzr
	bl	memset
	b	.LBB41_5
.LBB41_10:
	mov	w19, wzr
.LBB41_11:
	adrp	x9, __stack_chk_guard
	ldur	x8, [x29, #-8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB41_13
// %bb.12:
	mov	w0, w19
	ldp	x20, x19, [sp, #192]            // 16-byte Folded Reload
	ldp	x22, x21, [sp, #176]            // 16-byte Folded Reload
	ldp	x24, x23, [sp, #160]            // 16-byte Folded Reload
	ldp	x26, x25, [sp, #144]            // 16-byte Folded Reload
	ldp	x28, x27, [sp, #128]            // 16-byte Folded Reload
	ldp	x29, x30, [sp, #112]            // 16-byte Folded Reload
	add	sp, sp, #208                    // =208
	hint	#29
	ret
.LBB41_13:
	bl	__stack_chk_fail
.Lfunc_end41:
	.size	FtlVendorPartRead, .Lfunc_end41-FtlVendorPartRead
                                        // -- End function
	.globl	log2phys                        // -- Begin function log2phys
	.p2align	2
	.type	log2phys,@function
log2phys:                               // @log2phys
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-80]!           // 16-byte Folded Spill
	stp	x24, x23, [sp, #32]             // 16-byte Folded Spill
	adrp	x23, g_MaxLpn
	ldr	w8, [x23, :lo12:g_MaxLpn]
	adrp	x9, c_ftl_nand_sec_pre_page_shift
	stp	x22, x21, [sp, #48]             // 16-byte Folded Spill
	ldrh	w22, [x9, :lo12:c_ftl_nand_sec_pre_page_shift]
	stp	x20, x19, [sp, #64]             // 16-byte Folded Spill
	mov	w20, w2
	mov	w21, w0
	cmp	w8, w0
	mov	x19, x1
	stp	x26, x25, [sp, #16]             // 16-byte Folded Spill
	mov	x29, sp
	b.hi	.LBB42_2
// %bb.1:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.log2phys
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.log2phys
	mov	w2, #820
	bl	sftl_printk
	ldr	w8, [x23, :lo12:g_MaxLpn]
.LBB42_2:
	cmp	w8, w21
	b.ls	.LBB42_17
// %bb.3:
	add	x8, x22, #7                     // =7
	mov	x9, #-1
	adrp	x10, c_ftl_nand_l2pmap_ram_region_num
	lsl	x11, x9, x8
	ldrh	w9, [x10, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	mov	w24, w21
	mvn	w25, w11
	lsr	w21, w21, w8
	adrp	x23, p_l2p_ram_map
	cbz	x9, .LBB42_19
// %bb.4:
	ldr	x8, [x23, :lo12:p_l2p_ram_map]
	mov	x22, xzr
.LBB42_5:                               // =>This Inner Loop Header: Depth=1
	ldrh	w10, [x8]
	cmp	w10, w21, uxth
	b.eq	.LBB42_21
// %bb.6:                               //   in Loop: Header=BB42_5 Depth=1
	add	x22, x22, #1                    // =1
	cmp	x9, x22
	add	x8, x8, #16                     // =16
	b.ne	.LBB42_5
// %bb.7:
	ldr	x8, [x23, :lo12:p_l2p_ram_map]
	mov	x22, xzr
	mov	x10, x8
.LBB42_8:                               // =>This Inner Loop Header: Depth=1
	ldrsh	w11, [x10]
	cmn	w11, #1                         // =1
	b.eq	.LBB42_23
// %bb.9:                               //   in Loop: Header=BB42_8 Depth=1
	add	x22, x22, #1                    // =1
	cmp	x9, x22
	add	x10, x10, #16                   // =16
	b.ne	.LBB42_8
// %bb.10:
	mov	x10, xzr
	mov	w11, #-2147483648
	mov	w12, #4
	mov	w22, w9
.LBB42_11:                              // =>This Inner Loop Header: Depth=1
	ldr	w13, [x8, x12]
	add	x12, x12, #16                   // =16
	cmp	w13, #0                         // =0
	cset	w14, ge
	cmp	w13, w11
	cset	w15, lo
	tst	w14, w15
	csel	w22, w10, w22, ne
	add	x10, x10, #1                    // =1
	csel	w11, w13, w11, ne
	cmp	x9, x10
	b.ne	.LBB42_11
// %bb.12:
	cmp	w9, w22, uxth
	b.hi	.LBB42_25
// %bb.13:
	adrp	x11, g_l2p_last_update_region_id
	ldrh	w11, [x11, :lo12:g_l2p_last_update_region_id]
	mov	x10, xzr
	add	x12, x8, #4                     // =4
	mov	w13, #-1
	mov	w22, w9
	b	.LBB42_15
.LBB42_14:                              //   in Loop: Header=BB42_15 Depth=1
	add	x10, x10, #1                    // =1
	cmp	x9, x10
	add	x12, x12, #16                   // =16
	b.eq	.LBB42_24
.LBB42_15:                              // =>This Inner Loop Header: Depth=1
	ldr	w14, [x12]
	cmp	w14, w13
	b.hs	.LBB42_14
// %bb.16:                              //   in Loop: Header=BB42_15 Depth=1
	ldurh	w15, [x12, #-4]
	cmp	w11, w15
	csel	w22, w22, w10, eq
	csel	w13, w13, w14, eq
	b	.LBB42_14
.LBB42_17:
	mov	w0, #-1
	cbnz	w20, .LBB42_33
// %bb.18:
	str	w0, [x19]
	b	.LBB42_33
.LBB42_19:
	mov	w22, wzr
.LBB42_20:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.select_l2p_ram_region
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.select_l2p_ram_region
	mov	w2, #796
	bl	sftl_printk
	ldr	x8, [x23, :lo12:p_l2p_ram_map]
	b	.LBB42_25
.LBB42_21:
                                        // kill: def $w22 killed $w22 killed $x22 def $x22
	and	x9, x25, x24
	cbnz	w20, .LBB42_29
.LBB42_22:
	ldr	x8, [x23, :lo12:p_l2p_ram_map]
                                        // kill: def $w22 killed $w22 killed $x22 def $x22
	and	x9, x9, #0xffff
	add	x8, x8, w22, uxth #4
	ldr	x8, [x8, #8]
	ldr	w9, [x8, x9, lsl #2]
	and	x8, x22, #0xffff
	str	w9, [x19]
	b	.LBB42_30
.LBB42_23:
                                        // kill: def $w22 killed $w22 killed $x22 def $x22
	b	.LBB42_25
.LBB42_24:
	cmp	w9, w22, uxth
	b.ls	.LBB42_20
.LBB42_25:
	and	x9, x22, #0xffff
	lsl	x26, x9, #4
	ldrh	w1, [x8, x26]
	mov	w10, #65535
	cmp	w1, w10
	b.eq	.LBB42_28
// %bb.26:
	add	x9, x8, x9, lsl #4
	ldr	w9, [x9, #4]
	tbz	w9, #31, .LBB42_28
// %bb.27:
	add	x8, x8, x26
	ldr	x2, [x8, #8]
	adrp	x0, gL2pMapInfo
	add	x0, x0, :lo12:gL2pMapInfo
	bl	FtlMapWritePage
	ldr	x8, [x23, :lo12:p_l2p_ram_map]
	add	x8, x8, x26
	ldr	w9, [x8, #4]
	and	w9, w9, #0x7fffffff
	str	w9, [x8, #4]
.LBB42_28:
	mov	w0, w21
	mov	w1, w22
	bl	load_l2p_region
	and	x9, x25, x24
	cbz	w20, .LBB42_22
.LBB42_29:
	ldr	x10, [x23, :lo12:p_l2p_ram_map]
                                        // kill: def $w22 killed $w22 killed $x22 def $x22
	and	x8, x22, #0xffff
	lsl	x12, x8, #4
	ldr	w11, [x19]
	add	x10, x10, x12
	ldr	x10, [x10, #8]
	and	x9, x9, #0xffff
	str	w11, [x10, x9, lsl #2]
	ldr	x9, [x23, :lo12:p_l2p_ram_map]
	adrp	x11, g_l2p_last_update_region_id
	add	x9, x9, x12
	ldr	w10, [x9, #4]
	orr	w10, w10, #0x80000000
	str	w10, [x9, #4]
	strh	w21, [x11, :lo12:g_l2p_last_update_region_id]
.LBB42_30:
	ldr	x9, [x23, :lo12:p_l2p_ram_map]
	add	x8, x9, x8, lsl #4
	ldr	w9, [x8, #4]!
	cmn	w9, #1                          // =1
	b.eq	.LBB42_32
// %bb.31:
	mov	w0, wzr
	add	w9, w9, #1                      // =1
	str	w9, [x8]
	b	.LBB42_33
.LBB42_32:
	mov	w0, wzr
.LBB42_33:
	ldp	x20, x19, [sp, #64]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #48]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #32]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #16]             // 16-byte Folded Reload
	ldp	x29, x30, [sp], #80             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end42:
	.size	log2phys, .Lfunc_end42-log2phys
                                        // -- End function
	.globl	FlashReadPages                  // -- Begin function FlashReadPages
	.p2align	2
	.type	FlashReadPages,@function
FlashReadPages:                         // @FlashReadPages
// %bb.0:
	hint	#25
	sub	sp, sp, #112                    // =112
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	stp	x28, x27, [sp, #32]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #48]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #64]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #80]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #96]             // 16-byte Folded Spill
	add	x29, sp, #16                    // =16
	cbz	w1, .LBB43_26
// %bb.1:
	adrp	x8, g_nand_phy_info+12
	ldrh	w8, [x8, :lo12:g_nand_phy_info+12]
	adrp	x20, .L__func__.FlashReadPages
	adrp	x22, g_nand_phy_info+8
	adrp	x24, .L.str.117
	mov	w26, w1
	add	x21, x0, #16                    // =16
	add	x20, x20, :lo12:.L__func__.FlashReadPages
	add	x22, x22, :lo12:g_nand_phy_info+8
	adrp	x25, sftl_nand_check_buf
	adrp	x19, g_nand_ops+24
	add	x24, x24, :lo12:.L.str.117
	stur	w8, [x29, #-4]                  // 4-byte Folded Spill
	b	.LBB43_4
.LBB43_2:                               //   in Loop: Header=BB43_4 Depth=1
	mov	w8, #-1
	stur	w8, [x21, #-16]
.LBB43_3:                               //   in Loop: Header=BB43_4 Depth=1
	subs	x26, x26, #1                    // =1
	add	x21, x21, #32                   // =32
	b.eq	.LBB43_26
.LBB43_4:                               // =>This Inner Loop Header: Depth=1
	ldur	x8, [x21, #-8]
	cbz	x8, .LBB43_6
// %bb.5:                               //   in Loop: Header=BB43_4 Depth=1
	ldr	x8, [x21]
	cbnz	x8, .LBB43_7
.LBB43_6:                               //   in Loop: Header=BB43_4 Depth=1
	adrp	x0, .L.str.78
	mov	w2, #96
	add	x0, x0, :lo12:.L.str.78
	mov	x1, x20
	bl	sftl_printk
.LBB43_7:                               //   in Loop: Header=BB43_4 Depth=1
	ldrh	w8, [x22, #6]
	ldrh	w10, [x22]
	ldur	w9, [x21, #-12]
	cmp	w8, #4                          // =4
	cset	w8, eq
	lsr	w10, w10, w8
	ubfx	w11, w9, #10, #16
	udiv	w23, w11, w10
	cmp	w23, #4                         // =4
	b.hs	.LBB43_2
// %bb.8:                               //   in Loop: Header=BB43_4 Depth=1
	ldrh	w12, [x22, #2]
	msub	w10, w10, w23, w11
	ldp	x11, x3, [x21, #-8]
	ldr	x13, [x25, :lo12:sftl_nand_check_buf]
	ldr	x14, [x19, :lo12:g_nand_ops+24]
	lsl	w8, w12, w8
	and	w9, w9, #0x3ff
	tst	x11, #0x3f
	and	w8, w8, #0xffff
	madd	w28, w10, w8, w9
	csel	x27, x11, x13, eq
	mov	w0, w23
	mov	w1, w28
	mov	x2, x27
	blr	x14
	stur	w0, [x21, #-16]
	adrp	x8, g_nand_phy_info+14
	ldrh	w8, [x8, :lo12:g_nand_phy_info+14]
	cmp	w8, #4                          // =4
	b.ne	.LBB43_23
// %bb.9:                               //   in Loop: Header=BB43_4 Depth=1
	ldr	x8, [x21]
	ldur	w10, [x29, #-4]                 // 4-byte Folded Reload
	ldr	x9, [x19, :lo12:g_nand_ops+24]
	add	x2, x27, #2048                  // =2048
	add	x3, x8, #8                      // =8
	add	w1, w28, w10
	mov	w0, w23
	blr	x9
	cmn	w0, #1                          // =1
	b.eq	.LBB43_13
// %bb.10:                              //   in Loop: Header=BB43_4 Depth=1
	ldr	x8, [x21]
	ldr	w9, [x8, #12]
	cmn	w9, #1                          // =1
	b.ne	.LBB43_14
// %bb.11:                              //   in Loop: Header=BB43_4 Depth=1
	ldr	w9, [x8, #8]
	cmn	w9, #1                          // =1
	b.ne	.LBB43_14
// %bb.12:                              //   in Loop: Header=BB43_4 Depth=1
	ldr	w8, [x8]
	cmn	w8, #1                          // =1
	b.eq	.LBB43_14
.LBB43_13:                              //   in Loop: Header=BB43_4 Depth=1
	mov	w3, #-1
	stur	w3, [x21, #-16]
	b	.LBB43_19
.LBB43_14:                              //   in Loop: Header=BB43_4 Depth=1
	ldur	w3, [x21, #-16]
	cmp	w0, #256                        // =256
	b.ne	.LBB43_17
// %bb.15:                              //   in Loop: Header=BB43_4 Depth=1
	cmn	w3, #1                          // =1
	b.eq	.LBB43_17
// %bb.16:                              //   in Loop: Header=BB43_4 Depth=1
	mov	w3, #256
	stur	w3, [x21, #-16]
	b	.LBB43_19
.LBB43_17:                              //   in Loop: Header=BB43_4 Depth=1
	cmp	w3, #256                        // =256
	b.eq	.LBB43_19
// %bb.18:                              //   in Loop: Header=BB43_4 Depth=1
	cmn	w3, #1                          // =1
	b.ne	.LBB43_23
.LBB43_19:                              //   in Loop: Header=BB43_4 Depth=1
	ldur	w1, [x21, #-12]
	adrp	x0, .L.str.121
	add	x0, x0, :lo12:.L.str.121
	mov	w2, w28
	bl	sftl_printk
	ldur	x23, [x21, #-8]
	cbz	x23, .LBB43_21
// %bb.20:                              //   in Loop: Header=BB43_4 Depth=1
	adrp	x0, .L.str.116
	adrp	x1, .L.str.122
	add	x0, x0, :lo12:.L.str.116
	add	x1, x1, :lo12:.L.str.122
	mov	x2, x23
	mov	w3, wzr
	bl	sftl_printk
	ldr	w1, [x23]
	mov	x0, x24
	bl	sftl_printk
	ldr	w1, [x23, #4]
	mov	x0, x24
	bl	sftl_printk
	ldr	w1, [x23, #8]
	mov	x0, x24
	bl	sftl_printk
	ldr	w1, [x23, #12]
	mov	x0, x24
	bl	sftl_printk
	adrp	x0, .L.str.120
	add	x0, x0, :lo12:.L.str.120
	bl	sftl_printk
.LBB43_21:                              //   in Loop: Header=BB43_4 Depth=1
	ldr	x23, [x21]
	cbz	x23, .LBB43_23
// %bb.22:                              //   in Loop: Header=BB43_4 Depth=1
	adrp	x0, .L.str.116
	adrp	x1, .L.str.88
	add	x0, x0, :lo12:.L.str.116
	add	x1, x1, :lo12:.L.str.88
	mov	x2, x23
	mov	w3, wzr
	bl	sftl_printk
	ldr	w1, [x23]
	mov	x0, x24
	bl	sftl_printk
	ldr	w1, [x23, #4]
	mov	x0, x24
	bl	sftl_printk
	ldr	w1, [x23, #8]
	mov	x0, x24
	bl	sftl_printk
	ldr	w1, [x23, #12]
	mov	x0, x24
	bl	sftl_printk
	adrp	x0, .L.str.120
	add	x0, x0, :lo12:.L.str.120
	bl	sftl_printk
.LBB43_23:                              //   in Loop: Header=BB43_4 Depth=1
	ldr	x8, [x25, :lo12:sftl_nand_check_buf]
	cmp	x27, x8
	b.ne	.LBB43_3
// %bb.24:                              //   in Loop: Header=BB43_4 Depth=1
	ldur	x0, [x21, #-8]
	cmp	x27, x0
	b.eq	.LBB43_3
// %bb.25:                              //   in Loop: Header=BB43_4 Depth=1
	adrp	x8, c_ftl_nand_sec_pre_page
	ldrh	w8, [x8, :lo12:c_ftl_nand_sec_pre_page]
	mov	x1, x27
	lsl	x2, x8, #9
	bl	memcpy
	b	.LBB43_3
.LBB43_26:
	ldp	x20, x19, [sp, #96]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #80]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #64]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #48]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	add	sp, sp, #112                    // =112
	hint	#29
	ret
.Lfunc_end43:
	.size	FlashReadPages, .Lfunc_end43-FlashReadPages
                                        // -- End function
	.globl	ftl_memcpy                      // -- Begin function ftl_memcpy
	.p2align	2
	.type	ftl_memcpy,@function
ftl_memcpy:                             // @ftl_memcpy
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	mov	w2, w2
	str	x19, [sp, #16]                  // 8-byte Folded Spill
	mov	x29, sp
	mov	x19, x0
	bl	memcpy
	mov	x0, x19
	ldr	x19, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end44:
	.size	ftl_memcpy, .Lfunc_end44-ftl_memcpy
                                        // -- End function
	.globl	P2V_block_in_plane              // -- Begin function P2V_block_in_plane
	.p2align	2
	.type	P2V_block_in_plane,@function
P2V_block_in_plane:                     // @P2V_block_in_plane
// %bb.0:
	hint	#25
	adrp	x8, c_ftl_nand_blks_per_die
	ldrh	w8, [x8, :lo12:c_ftl_nand_blks_per_die]
	adrp	x9, c_ftl_nand_planes_per_die
	ldrh	w9, [x9, :lo12:c_ftl_nand_planes_per_die]
	and	w10, w0, #0xffff
	udiv	w11, w10, w8
	msub	w8, w11, w8, w10
	udiv	w0, w8, w9
	hint	#29
	ret
.Lfunc_end45:
	.size	P2V_block_in_plane, .Lfunc_end45-P2V_block_in_plane
                                        // -- End function
	.globl	FtlGcRefreshBlock               // -- Begin function FtlGcRefreshBlock
	.p2align	2
	.type	FtlGcRefreshBlock,@function
FtlGcRefreshBlock:                      // @FtlGcRefreshBlock
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #16]             // 16-byte Folded Spill
	mov	w19, w0
	and	w20, w0, #0xffff
	adrp	x0, .L.str.112
	add	x0, x0, :lo12:.L.str.112
	mov	w1, w20
	mov	x29, sp
	bl	sftl_printk
	adrp	x8, g_gc_next_blk
	ldrh	w10, [x8, :lo12:g_gc_next_blk]
	cmp	w10, w20
	b.eq	.LBB46_6
// %bb.1:
	adrp	x9, g_gc_next_blk_1
	ldrh	w11, [x9, :lo12:g_gc_next_blk_1]
	cmp	w11, w19, uxth
	b.eq	.LBB46_6
// %bb.2:
	mov	w12, #65535
	cmp	w10, w12
	b.eq	.LBB46_5
// %bb.3:
	cmp	w11, w12
	b.ne	.LBB46_6
// %bb.4:
	strh	w19, [x9, :lo12:g_gc_next_blk_1]
	b	.LBB46_6
.LBB46_5:
	strh	w19, [x8, :lo12:g_gc_next_blk]
.LBB46_6:
	ldp	x20, x19, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end46:
	.size	FtlGcRefreshBlock, .Lfunc_end46-FtlGcRefreshBlock
                                        // -- End function
	.globl	FtlProgPages                    // -- Begin function FtlProgPages
	.p2align	2
	.type	FtlProgPages,@function
FtlProgPages:                           // @FtlProgPages
// %bb.0:
	hint	#25
	sub	sp, sp, #112                    // =112
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	mov	w2, wzr
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	stp	x28, x27, [sp, #32]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #48]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #64]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #80]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #96]             // 16-byte Folded Spill
	add	x29, sp, #16                    // =16
	mov	x19, x3
	mov	w21, w1
	mov	x20, x0
	str	x8, [sp, #8]
	bl	FlashProgPages
	adrp	x26, c_ftl_nand_planes_num
	cbz	w21, .LBB47_15
// %bb.1:
	mov	w28, w21
	adrp	x21, .L.str.77
	mov	x27, xzr
	add	x21, x21, :lo12:.L.str.77
	adrp	x22, c_ftl_nand_blks_per_die
	adrp	x23, c_ftl_nand_planes_per_die
	b	.LBB47_4
.LBB47_2:                               //   in Loop: Header=BB47_4 Depth=1
	mov	w0, w25
	bl	decrement_vpc_count
.LBB47_3:                               //   in Loop: Header=BB47_4 Depth=1
	add	x27, x27, #1                    // =1
	cmp	x27, x28
	b.eq	.LBB47_15
.LBB47_4:                               // =>This Loop Header: Depth=1
                                        //     Child Loop BB47_7 Depth 2
	add	x25, x20, x27, lsl #5
	add	x24, x25, #4                    // =4
	b	.LBB47_7
.LBB47_5:                               //   in Loop: Header=BB47_7 Depth=2
	ldr	w1, [x24]
	mov	x0, x21
	bl	sftl_printk
	ldr	w8, [x24]
	ldrh	w9, [x22, :lo12:c_ftl_nand_blks_per_die]
	ldrh	w10, [x23, :lo12:c_ftl_nand_planes_per_die]
	ubfx	w8, w8, #10, #16
	udiv	w11, w8, w9
	msub	w8, w11, w9, w8
	udiv	w0, w8, w10
	bl	decrement_vpc_count
	ldrh	w8, [x19, #4]
	cbz	w8, .LBB47_9
.LBB47_6:                               //   in Loop: Header=BB47_7 Depth=2
	mov	x0, x19
	bl	get_new_active_ppa
	str	w0, [x24]
	mov	w1, #1
	mov	x0, x25
	mov	w2, wzr
	bl	FlashProgPages
.LBB47_7:                               //   Parent Loop BB47_4 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldr	w2, [x25]
	cmp	w2, #256                        // =256
	b.eq	.LBB47_5
// %bb.8:                               //   in Loop: Header=BB47_7 Depth=2
	cmn	w2, #1                          // =1
	b.eq	.LBB47_5
	b	.LBB47_10
.LBB47_9:                               //   in Loop: Header=BB47_7 Depth=2
	mov	x0, x19
	bl	allocate_new_data_superblock
	b	.LBB47_6
.LBB47_10:                              //   in Loop: Header=BB47_4 Depth=1
	ldrb	w8, [x19, #6]
	ldrh	w9, [x26, :lo12:c_ftl_nand_planes_num]
	cmp	w9, w8
	b.hi	.LBB47_12
// %bb.11:                              //   in Loop: Header=BB47_4 Depth=1
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlProgPages
	mov	w2, #985
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlProgPages
	bl	sftl_printk
.LBB47_12:                              //   in Loop: Header=BB47_4 Depth=1
	ldr	w8, [x24]
	add	x24, x20, x27, lsl #5
	add	x1, sp, #4                      // =4
	mov	w2, #1
	str	w8, [sp, #4]
	ldr	w0, [x24, #24]
	bl	log2phys
	ldr	x8, [x24, #16]
	ldr	w8, [x8, #12]
	cmn	w8, #1                          // =1
	b.eq	.LBB47_3
// %bb.13:                              //   in Loop: Header=BB47_4 Depth=1
	ldrh	w9, [x22, :lo12:c_ftl_nand_blks_per_die]
	ldrh	w10, [x23, :lo12:c_ftl_nand_planes_per_die]
	adrp	x11, p_valid_page_count_table
	ubfx	w8, w8, #10, #16
	ldr	x11, [x11, :lo12:p_valid_page_count_table]
	udiv	w12, w8, w9
	msub	w8, w12, w9, w8
	udiv	w25, w8, w10
	ldrh	w8, [x11, w25, uxtw #1]
	cbnz	w8, .LBB47_2
// %bb.14:                              //   in Loop: Header=BB47_4 Depth=1
	adrp	x0, .L.str.79
	and	w1, w25, #0xffff
	add	x0, x0, :lo12:.L.str.79
	mov	w2, wzr
	bl	sftl_printk
	b	.LBB47_2
.LBB47_15:
	ldrb	w8, [x19, #6]
	ldrh	w9, [x26, :lo12:c_ftl_nand_planes_num]
	cmp	w9, w8
	b.hi	.LBB47_17
// %bb.16:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlProgPages
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlProgPages
	mov	w2, #1000
	bl	sftl_printk
.LBB47_17:
	adrp	x9, __stack_chk_guard
	ldr	x8, [sp, #8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB47_19
// %bb.18:
	ldp	x20, x19, [sp, #96]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #80]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #64]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #48]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	add	sp, sp, #112                    // =112
	hint	#29
	ret
.LBB47_19:
	bl	__stack_chk_fail
.Lfunc_end47:
	.size	FtlProgPages, .Lfunc_end47-FtlProgPages
                                        // -- End function
	.globl	decrement_vpc_count             // -- Begin function decrement_vpc_count
	.p2align	2
	.type	decrement_vpc_count,@function
decrement_vpc_count:                    // @decrement_vpc_count
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-48]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #32]             // 16-byte Folded Spill
	and	w19, w0, #0xffff
	mov	w8, #65535
	mov	w20, w0
	cmp	w19, w8
	stp	x22, x21, [sp, #16]             // 16-byte Folded Spill
	mov	x29, sp
	b.eq	.LBB48_3
// %bb.1:
	adrp	x22, p_valid_page_count_table
	ldr	x9, [x22, :lo12:p_valid_page_count_table]
	and	x21, x20, #0xffff
	ldrh	w10, [x9, x21, lsl #1]
	cbz	w10, .LBB48_9
// %bb.2:
	sub	w10, w10, #1                    // =1
	strh	w10, [x9, x21, lsl #1]
.LBB48_3:
	adrp	x22, g_tmp_data_superblock_id
	ldrh	w0, [x22, :lo12:g_tmp_data_superblock_id]
	cmp	w0, w8
	b.eq	.LBB48_8
// %bb.4:
	cmp	w0, w20, uxth
	b.eq	.LBB48_11
// %bb.5:
	bl	update_vpc_list
	adrp	x8, p_data_block_list_head
	adrp	x9, p_data_block_list_table
	ldr	x8, [x8, :lo12:p_data_block_list_head]
	ldr	x9, [x9, :lo12:p_data_block_list_table]
	adrp	x10, p_valid_page_count_table
	ldr	x10, [x10, :lo12:p_valid_page_count_table]
	strh	w20, [x22, :lo12:g_tmp_data_superblock_id]
	sub	x8, x8, x9
	mov	x9, #-6148914691236517206
	asr	x8, x8, #1
	movk	x9, #43691
	mul	x8, x8, x9
	and	x9, x8, #0xffff
	ldrh	w9, [x10, x9, lsl #1]
	cmp	w0, #0                          // =0
	cset	w21, ne
	cbnz	w9, .LBB48_12
// %bb.6:
	cmp	w19, w8, uxth
	b.eq	.LBB48_12
// %bb.7:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.decrement_vpc_count
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.decrement_vpc_count
	mov	w2, #2869
	bl	sftl_printk
	b	.LBB48_12
.LBB48_8:
	mov	w21, wzr
	strh	w20, [x22, :lo12:g_tmp_data_superblock_id]
	b	.LBB48_12
.LBB48_9:
	adrp	x0, .L.str.110
	add	x0, x0, :lo12:.L.str.110
	mov	w1, w19
	mov	w2, wzr
	bl	sftl_printk
	ldr	x8, [x22, :lo12:p_valid_page_count_table]
	ldrh	w8, [x8, x21, lsl #1]
	mov	w21, wzr
	cbnz	w8, .LBB48_12
// %bb.10:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.decrement_vpc_count
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.decrement_vpc_count
	mov	w2, #2853
	bl	sftl_printk
.LBB48_11:
	mov	w21, wzr
.LBB48_12:
	mov	w0, w21
	ldp	x20, x19, [sp, #32]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #16]             // 16-byte Folded Reload
	ldp	x29, x30, [sp], #48             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end48:
	.size	decrement_vpc_count, .Lfunc_end48-decrement_vpc_count
                                        // -- End function
	.globl	allocate_new_data_superblock    // -- Begin function allocate_new_data_superblock
	.p2align	2
	.type	allocate_new_data_superblock,@function
allocate_new_data_superblock:           // @allocate_new_data_superblock
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-64]!           // 16-byte Folded Spill
	stp	x22, x21, [sp, #32]             // 16-byte Folded Spill
	adrp	x22, c_ftl_nand_data_blks_per_plane
	stp	x20, x19, [sp, #48]             // 16-byte Folded Spill
	ldrh	w20, [x0]
	ldrh	w8, [x22, :lo12:c_ftl_nand_data_blks_per_plane]
	mov	x19, x0
	str	x23, [sp, #16]                  // 8-byte Folded Spill
	mov	x29, sp
	cmp	w20, w8
	b.ls	.LBB49_2
// %bb.1:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.allocate_new_data_superblock
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.allocate_new_data_superblock
	mov	w2, #2766
	bl	sftl_printk
.LBB49_2:
	mov	w21, #65535
	cmp	w20, w21
	b.eq	.LBB49_7
// %bb.3:
	adrp	x8, p_valid_page_count_table
	ldr	x8, [x8, :lo12:p_valid_page_count_table]
	ldrh	w8, [x8, x20, lsl #1]
	cbz	w8, .LBB49_6
// %bb.4:
	mov	w0, w20
	bl	insert_data_list
	adrp	x8, g_num_data_superblocks
	ldrh	w9, [x8, :lo12:g_num_data_superblocks]
	ldrh	w10, [x22, :lo12:c_ftl_nand_data_blks_per_plane]
	add	w9, w9, #1                      // =1
	cmp	w10, w9, uxth
	strh	w9, [x8, :lo12:g_num_data_superblocks]
	b.hs	.LBB49_7
// %bb.5:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.INSERT_DATA_LIST
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.INSERT_DATA_LIST
	mov	w2, #214
	bl	sftl_printk
	b	.LBB49_7
.LBB49_6:
	mov	w0, w20
	bl	INSERT_FREE_LIST
.LBB49_7:
	mov	w8, #1
	strb	w8, [x19, #8]
	adrp	x22, g_tmp_data_superblock_id
	ldrh	w0, [x22, :lo12:g_tmp_data_superblock_id]
	cmp	w0, w21
	b.eq	.LBB49_12
// %bb.8:
	cmp	w20, w0
	b.ne	.LBB49_10
// %bb.9:
	adrp	x8, p_valid_page_count_table
	ldr	x8, [x8, :lo12:p_valid_page_count_table]
	ldrh	w8, [x8, x20, lsl #1]
	cbz	w8, .LBB49_11
.LBB49_10:
	bl	update_vpc_list
.LBB49_11:
	mov	w8, #65535
	strh	w8, [x22, :lo12:g_tmp_data_superblock_id]
.LBB49_12:
	mov	x0, x19
	bl	allocate_data_superblock
	bl	FtlWriteDump_data
	adrp	x20, c_ftl_nand_l2pmap_ram_region_num
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	cbz	w8, .LBB49_17
// %bb.13:
	adrp	x19, gL2pMapInfo
	mov	x21, xzr
	mov	x22, xzr
	adrp	x23, p_l2p_ram_map
	add	x19, x19, :lo12:gL2pMapInfo
	b	.LBB49_15
.LBB49_14:                              //   in Loop: Header=BB49_15 Depth=1
	add	x22, x22, #1                    // =1
	cmp	x22, w8, uxth
	add	x21, x21, #16                   // =16
	b.hs	.LBB49_17
.LBB49_15:                              // =>This Inner Loop Header: Depth=1
	ldr	x9, [x23, :lo12:p_l2p_ram_map]
	add	x9, x9, x21
	ldr	w10, [x9, #4]
	tbz	w10, #31, .LBB49_14
// %bb.16:                              //   in Loop: Header=BB49_15 Depth=1
	ldrh	w1, [x9]
	ldr	x2, [x9, #8]
	mov	x0, x19
	bl	FtlMapWritePage
	ldr	x8, [x23, :lo12:p_l2p_ram_map]
	add	x8, x8, x21
	ldr	w9, [x8, #4]
	and	w9, w9, #0x7fffffff
	str	w9, [x8, #4]
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	b	.LBB49_14
.LBB49_17:
	mov	w0, wzr
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	ldp	x20, x19, [sp, #48]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #32]             // 16-byte Folded Reload
	ldr	x23, [sp, #16]                  // 8-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #64             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end49:
	.size	allocate_new_data_superblock, .Lfunc_end49-allocate_new_data_superblock
                                        // -- End function
	.globl	get_new_active_ppa              // -- Begin function get_new_active_ppa
	.p2align	2
	.type	get_new_active_ppa,@function
get_new_active_ppa:                     // @get_new_active_ppa
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	ldrsh	w8, [x0]
	stp	x20, x19, [sp, #16]             // 16-byte Folded Spill
	mov	x19, x0
	mov	x29, sp
	cmn	w8, #1                          // =1
	b.ne	.LBB50_2
// %bb.1:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.get_new_active_ppa
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.get_new_active_ppa
	mov	w2, #2792
	bl	sftl_printk
.LBB50_2:
	adrp	x20, c_ftl_nand_page_pre_blk
	ldrh	w8, [x19, #2]
	ldrh	w9, [x20, :lo12:c_ftl_nand_page_pre_blk]
	cmp	w8, w9
	b.ne	.LBB50_4
// %bb.3:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.get_new_active_ppa
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.get_new_active_ppa
	mov	w2, #2793
	bl	sftl_printk
.LBB50_4:
	ldrh	w8, [x19, #4]
	cbnz	w8, .LBB50_6
// %bb.5:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.get_new_active_ppa
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.get_new_active_ppa
	mov	w2, #2794
	bl	sftl_printk
.LBB50_6:
	ldrb	w8, [x19, #6]
	strb	wzr, [x19, #10]
	add	x9, x19, x8, lsl #1
	ldrh	w12, [x9, #16]
	mov	w9, #65535
                                        // kill: def $w8 killed $w8 killed $x8 def $x8
	cmp	w12, w9
	b.ne	.LBB50_12
// %bb.7:
	adrp	x10, c_ftl_nand_planes_num
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_num]
	b	.LBB50_9
.LBB50_8:                               //   in Loop: Header=BB50_9 Depth=1
	add	x11, x19, w8, uxtb #1
	ldrh	w12, [x11, #16]
	cmp	w12, w9
	b.ne	.LBB50_11
.LBB50_9:                               // =>This Inner Loop Header: Depth=1
	add	w8, w8, #1                      // =1
	cmp	w10, w8, uxtb
	b.ne	.LBB50_8
// %bb.10:                              //   in Loop: Header=BB50_9 Depth=1
	ldrh	w11, [x19, #2]
	mov	w8, wzr
	add	w11, w11, #1                    // =1
	strh	w11, [x19, #2]
	b	.LBB50_8
.LBB50_11:
	strb	w8, [x19, #6]
.LBB50_12:
	ldrh	w10, [x19, #2]
	ldrh	w9, [x20, :lo12:c_ftl_nand_page_pre_blk]
	cmp	w10, w9
	b.hs	.LBB50_17
// %bb.13:
	ldrh	w11, [x19, #4]
	adrp	x13, c_ftl_nand_planes_num
                                        // kill: def $w8 killed $w8 killed $x8 def $x8
	sub	w11, w11, #1                    // =1
	strh	w11, [x19, #4]
	ldrh	w14, [x13, :lo12:c_ftl_nand_planes_num]
	lsl	w13, w12, #10
	mov	w12, w10
	b	.LBB50_15
.LBB50_14:                              //   in Loop: Header=BB50_15 Depth=1
	add	x15, x19, w8, uxtb #1
	ldrsh	w15, [x15, #16]
	cmn	w15, #1                         // =1
	b.ne	.LBB50_18
.LBB50_15:                              // =>This Inner Loop Header: Depth=1
	add	w8, w8, #1                      // =1
	cmp	w14, w8, uxtb
	b.ne	.LBB50_14
// %bb.16:                              //   in Loop: Header=BB50_15 Depth=1
	mov	w8, wzr
	add	w12, w12, #1                    // =1
	strh	w12, [x19, #2]
	b	.LBB50_14
.LBB50_17:
	mov	w20, #65535
	b	.LBB50_21
.LBB50_18:
	orr	w20, w13, w10
	tst	w11, #0xffff
	strb	w8, [x19, #6]
	b.eq	.LBB50_21
// %bb.19:
	cmp	w9, w12, uxth
	b.ne	.LBB50_21
// %bb.20:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.get_new_active_ppa
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.get_new_active_ppa
	mov	w2, #2817
	bl	sftl_printk
.LBB50_21:
	mov	w0, w20
	ldp	x20, x19, [sp, #16]             // 16-byte Folded Reload
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end50:
	.size	get_new_active_ppa, .Lfunc_end50-get_new_active_ppa
                                        // -- End function
	.globl	FtlWrite                        // -- Begin function FtlWrite
	.p2align	2
	.type	FtlWrite,@function
FtlWrite:                               // @FtlWrite
// %bb.0:
	hint	#25
	sub	sp, sp, #192                    // =192
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	and	w9, w0, #0xff
	stp	x29, x30, [sp, #96]             // 16-byte Folded Spill
	add	x29, sp, #96                    // =96
	cmp	w9, #16                         // =16
	stp	x28, x27, [sp, #112]            // 16-byte Folded Spill
	stp	x26, x25, [sp, #128]            // 16-byte Folded Spill
	stp	x24, x23, [sp, #144]            // 16-byte Folded Spill
	stp	x22, x21, [sp, #160]            // 16-byte Folded Spill
	stp	x20, x19, [sp, #176]            // 16-byte Folded Spill
	stur	x8, [x29, #-8]
	b.ne	.LBB51_2
// %bb.1:
	add	w0, w1, #256                    // =256
	mov	w1, w2
	mov	x2, x3
	bl	FtlVendorPartWrite
	b	.LBB51_21
.LBB51_2:
	adrp	x8, g_MaxLbaSector
	ldr	w8, [x8, :lo12:g_MaxLbaSector]
	mov	w0, #-1
	cmp	w8, w1
	b.ls	.LBB51_21
// %bb.3:
	cmp	w8, w2
	b.lo	.LBB51_21
// %bb.4:
	add	w9, w2, w1
	cmp	w9, w8
	mov	w0, #-1
	str	w9, [sp, #24]                   // 4-byte Folded Spill
	b.hi	.LBB51_21
// %bb.5:
	adrp	x8, gFtlInitStatus
	ldr	w8, [x8, :lo12:gFtlInitStatus]
	cmn	w8, #1                          // =1
	b.eq	.LBB51_21
// %bb.6:
	adrp	x11, g_totle_write_sector
	ldr	w12, [x11, :lo12:g_totle_write_sector]
	adrp	x8, g_ftl_nand_free_count
	mov	w9, #2048
	adrp	x13, c_ftl_nand_sec_pre_page
	ldr	w10, [sp, #24]                  // 4-byte Folded Reload
	str	w9, [x8, :lo12:g_ftl_nand_free_count]
	ldrh	w9, [x13, :lo12:c_ftl_nand_sec_pre_page]
	adrp	x8, g_totle_write_page_count
	add	w12, w12, w2
	sub	w10, w10, #1                    // =1
	str	w12, [x11, :lo12:g_totle_write_sector]
	ldr	w11, [x8, :lo12:g_totle_write_page_count]
	udiv	w24, w1, w9
	udiv	w9, w10, w9
	stp	w9, w1, [sp, #32]               // 8-byte Folded Spill
	sub	w9, w9, w24
	adds	w20, w9, #1                     // =1
	add	w9, w11, w20
	str	x3, [sp, #40]                   // 8-byte Folded Spill
	str	w2, [sp, #20]                   // 4-byte Folded Spill
	str	w9, [x8, :lo12:g_totle_write_page_count]
	b.lo	.LBB51_23
.LBB51_7:
	mov	w0, wzr
	bl	rk_ftl_garbage_collect
	adrp	x21, g_num_free_superblocks
	ldrh	w8, [x21, :lo12:g_num_free_superblocks]
	mov	w0, wzr
	cmp	w8, #5                          // =5
	b.hi	.LBB51_21
// %bb.8:
	mov	w8, #255
	adrp	x22, g_gc_superblock
	adrp	x23, g_gc_next_blk
	mov	w24, #65535
	adrp	x26, g_gc_merge_free_blk_threshold
	mov	w27, #128
	adrp	x28, g_gc_free_blk_threshold
.LBB51_9:                               // =>This Inner Loop Header: Depth=1
	ldrh	w9, [x22, :lo12:g_gc_superblock]
	ldrh	w10, [x23, :lo12:g_gc_next_blk]
	mov	w25, w8
	and	w9, w10, w9
	cmp	w9, w24
	b.ne	.LBB51_19
// %bb.10:                              //   in Loop: Header=BB51_9 Depth=1
	adrp	x8, p_data_block_list_head
	ldr	x8, [x8, :lo12:p_data_block_list_head]
	cbz	x8, .LBB51_12
// %bb.11:                              //   in Loop: Header=BB51_9 Depth=1
	adrp	x9, p_data_block_list_table
	ldr	x9, [x9, :lo12:p_data_block_list_table]
	sub	x8, x8, x9
	mov	w9, #43691
	lsr	x8, x8, #1
	movk	w9, #43690, lsl #16
	mul	w19, w8, w9
	b	.LBB51_13
.LBB51_12:                              //   in Loop: Header=BB51_9 Depth=1
	mov	w19, #65535
.LBB51_13:                              //   in Loop: Header=BB51_9 Depth=1
	and	w20, w19, #0xffff
	adrp	x0, .L.str.112
	add	x0, x0, :lo12:.L.str.112
	mov	w1, w20
	bl	sftl_printk
	ldrh	w8, [x23, :lo12:g_gc_next_blk]
	cmp	w8, w20
	b.eq	.LBB51_19
// %bb.14:                              //   in Loop: Header=BB51_9 Depth=1
	adrp	x9, g_gc_next_blk_1
	ldrh	w9, [x9, :lo12:g_gc_next_blk_1]
	cmp	w9, w19, uxth
	b.eq	.LBB51_19
// %bb.15:                              //   in Loop: Header=BB51_9 Depth=1
	cmp	w8, w24
	b.eq	.LBB51_18
// %bb.16:                              //   in Loop: Header=BB51_9 Depth=1
	cmp	w9, w24
	b.ne	.LBB51_19
// %bb.17:                              //   in Loop: Header=BB51_9 Depth=1
	adrp	x8, g_gc_next_blk_1
	strh	w19, [x8, :lo12:g_gc_next_blk_1]
	b	.LBB51_19
.LBB51_18:                              //   in Loop: Header=BB51_9 Depth=1
	strh	w19, [x23, :lo12:g_gc_next_blk]
.LBB51_19:                              //   in Loop: Header=BB51_9 Depth=1
	mov	w0, #1
	strh	w27, [x26, :lo12:g_gc_merge_free_blk_threshold]
	strh	w27, [x28, :lo12:g_gc_free_blk_threshold]
	bl	rk_ftl_garbage_collect
	mov	w0, wzr
	bl	rk_ftl_garbage_collect
	ldrh	w8, [x21, :lo12:g_num_free_superblocks]
	mov	w0, wzr
	cmp	w8, #2                          // =2
	b.hi	.LBB51_21
// %bb.20:                              //   in Loop: Header=BB51_9 Depth=1
	sub	w8, w25, #1                     // =1
	cbnz	w25, .LBB51_9
.LBB51_21:
	adrp	x9, __stack_chk_guard
	ldur	x8, [x29, #-8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB51_70
// %bb.22:
	ldp	x20, x19, [sp, #176]            // 16-byte Folded Reload
	ldp	x22, x21, [sp, #160]            // 16-byte Folded Reload
	ldp	x24, x23, [sp, #144]            // 16-byte Folded Reload
	ldp	x26, x25, [sp, #128]            // 16-byte Folded Reload
	ldp	x28, x27, [sp, #112]            // 16-byte Folded Reload
	ldp	x29, x30, [sp, #96]             // 16-byte Folded Reload
	add	sp, sp, #192                    // =192
	hint	#29
	ret
.LBB51_23:
	adrp	x10, g_active_superblock
	add	x10, x10, :lo12:g_active_superblock
	adrp	x9, power_up_flag
	ldrh	w8, [x10, #4]
	ldr	w9, [x9, :lo12:power_up_flag]
	adrp	x11, g_buffer_superblock
	add	x11, x11, :lo12:g_buffer_superblock
	cmp	w8, #0                          // =0
	ccmp	w9, #0, #4, eq
	mov	w19, wzr
	adrp	x22, req_prgm
	csel	x28, x11, x10, ne
	mov	w21, w24
	str	wzr, [sp, #28]                  // 4-byte Folded Spill
	b	.LBB51_25
.LBB51_24:                              //   in Loop: Header=BB51_25 Depth=1
	subs	w20, w20, w23
	b.eq	.LBB51_7
.LBB51_25:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB51_41 Depth 2
	adrp	x9, c_ftl_nand_planes_num
	ldrb	w8, [x28, #6]
	ldrh	w9, [x9, :lo12:c_ftl_nand_planes_num]
	cmp	w9, w8
	b.hi	.LBB51_27
// %bb.26:                              //   in Loop: Header=BB51_25 Depth=1
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlWrite
	mov	w2, #1041
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlWrite
	bl	sftl_printk
.LBB51_27:                              //   in Loop: Header=BB51_25 Depth=1
	ldrh	w8, [x28, #4]
	cbnz	w8, .LBB51_36
// %bb.28:                              //   in Loop: Header=BB51_25 Depth=1
	adrp	x8, g_active_superblock
	add	x8, x8, :lo12:g_active_superblock
	cmp	x28, x8
	b.ne	.LBB51_33
// %bb.29:                              //   in Loop: Header=BB51_25 Depth=1
	adrp	x8, g_buffer_superblock+4
	ldrh	w8, [x8, :lo12:g_buffer_superblock+4]
	mov	w25, w21
	cbnz	w8, .LBB51_31
// %bb.30:                              //   in Loop: Header=BB51_25 Depth=1
	adrp	x0, g_buffer_superblock
	add	x0, x0, :lo12:g_buffer_superblock
	bl	allocate_new_data_superblock
	adrp	x8, power_up_flag
	str	wzr, [x8, :lo12:power_up_flag]
.LBB51_31:                              //   in Loop: Header=BB51_25 Depth=1
	adrp	x21, g_active_superblock
	add	x21, x21, :lo12:g_active_superblock
	mov	x0, x21
	bl	allocate_new_data_superblock
	adrp	x8, power_up_flag
	ldr	w8, [x8, :lo12:power_up_flag]
	cmp	w8, #0                          // =0
	adrp	x8, g_buffer_superblock
	add	x8, x8, :lo12:g_buffer_superblock
	csel	x23, x21, x8, eq
	mov	w21, w25
	ldrh	w8, [x23, #4]
	cbnz	w8, .LBB51_35
.LBB51_32:                              //   in Loop: Header=BB51_25 Depth=1
	mov	x0, x23
	bl	allocate_new_data_superblock
	ldrh	w8, [x23, #4]
	b	.LBB51_35
.LBB51_33:                              //   in Loop: Header=BB51_25 Depth=1
	adrp	x23, g_active_superblock
	add	x23, x23, :lo12:g_active_superblock
	ldrh	w8, [x23, #4]
	adrp	x9, power_up_flag
	str	wzr, [x9, :lo12:power_up_flag]
	cbz	w8, .LBB51_69
// %bb.34:                              //   in Loop: Header=BB51_25 Depth=1
	ldrh	w8, [x23, #4]
	cbz	w8, .LBB51_32
.LBB51_35:                              //   in Loop: Header=BB51_25 Depth=1
	mov	x28, x23
.LBB51_36:                              //   in Loop: Header=BB51_25 Depth=1
	ldrb	w9, [x28, #7]
	adrp	x11, c_ftl_nand_planes_num
	ldrb	w10, [x28, #6]
	ldrh	w11, [x11, :lo12:c_ftl_nand_planes_num]
	lsl	w9, w9, #2
	cmp	w20, w9
	csel	w9, w9, w20, hi
	cmp	w9, w8
	csel	w23, w8, w9, hi
	cmp	w11, w10
	b.hi	.LBB51_38
// %bb.37:                              //   in Loop: Header=BB51_25 Depth=1
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlWrite
	mov	w2, #1074
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlWrite
	bl	sftl_printk
.LBB51_38:                              //   in Loop: Header=BB51_25 Depth=1
	cbz	w23, .LBB51_67
// %bb.39:                              //   in Loop: Header=BB51_25 Depth=1
	mov	w8, w23
	str	w8, [sp, #8]                    // 4-byte Folded Spill
	mov	w8, w8
	stp	w21, w20, [sp, #12]             // 8-byte Folded Spill
	mov	x25, xzr
	mov	x23, xzr
	lsl	x20, x8, #5
	mov	w21, w21
	b	.LBB51_41
.LBB51_40:                              //   in Loop: Header=BB51_41 Depth=2
	mov	w8, #61589
	strh	w8, [x26]
	adrp	x10, g_GlobalDataVersion
	ldr	w8, [x10, :lo12:g_GlobalDataVersion]
	add	x25, x25, #32                   // =32
	add	x23, x23, #1                    // =1
	add	w9, w8, #1                      // =1
	cmn	w9, #1                          // =1
	stp	w8, w27, [x26, #4]
	csinc	w8, wzr, w8, eq
	str	w8, [x10, :lo12:g_GlobalDataVersion]
	ldur	w8, [x29, #-12]
	cmp	x20, x25
	str	w8, [x26, #12]
	ldrh	w8, [x28]
	strh	w8, [x26, #2]
	b.eq	.LBB51_66
.LBB51_41:                              //   Parent Loop BB51_25 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldrh	w8, [x28, #4]
	cbz	w8, .LBB51_65
// %bb.42:                              //   in Loop: Header=BB51_41 Depth=2
	add	x27, x21, x23
	sub	x1, x29, #12                    // =12
	mov	w0, w27
	mov	w2, wzr
	bl	log2phys
	mov	x0, x28
	bl	get_new_active_ppa
	ldr	x8, [x22, :lo12:req_prgm]
	adrp	x11, c_ftl_nand_byte_pre_oob
	adrp	x9, p_io_spare_buf
	mov	w1, wzr
	add	x8, x8, x25
	str	w0, [x8, #4]
	ldrh	w8, [x11, :lo12:c_ftl_nand_byte_pre_oob]
	ldr	x9, [x9, :lo12:p_io_spare_buf]
	ldr	x10, [x22, :lo12:req_prgm]
	mul	w8, w23, w8
	and	w8, w8, #0xfffffffc
	add	x8, x9, x8
	add	x9, x10, x25
	str	x8, [x9, #16]
	ldr	x8, [x22, :lo12:req_prgm]
	add	x8, x8, x25
	str	w27, [x8, #24]
	ldr	x8, [x22, :lo12:req_prgm]
	ldrh	w2, [x11, :lo12:c_ftl_nand_byte_pre_oob]
	add	x8, x8, x25
	ldr	x26, [x8, #16]
	mov	x0, x26
	bl	memset
	adrp	x8, c_ftl_nand_sec_pre_page
	ldrh	w8, [x8, :lo12:c_ftl_nand_sec_pre_page]
	cmp	w27, w24
	b.eq	.LBB51_45
// %bb.43:                              //   in Loop: Header=BB51_41 Depth=2
	ldr	w9, [sp, #32]                   // 4-byte Folded Reload
	cmp	w27, w9
	b.eq	.LBB51_45
// %bb.44:                              //   in Loop: Header=BB51_41 Depth=2
	ldr	w9, [sp, #36]                   // 4-byte Folded Reload
	ldr	x10, [x22, :lo12:req_prgm]
	neg	w9, w9
	madd	w8, w27, w8, w9
	ldr	x9, [sp, #40]                   // 8-byte Folded Reload
	lsl	w8, w8, #9
	add	x8, x9, x8
	add	x9, x10, x25
	str	x8, [x9, #8]
	b	.LBB51_63
.LBB51_45:                              //   in Loop: Header=BB51_41 Depth=2
	cmp	w27, w24
	b.ne	.LBB51_48
// %bb.46:                              //   in Loop: Header=BB51_41 Depth=2
	ldr	w10, [sp, #36]                  // 4-byte Folded Reload
	udiv	w9, w10, w8
	msub	w9, w9, w8, w10
	ldr	w10, [sp, #20]                  // 4-byte Folded Reload
	str	w9, [sp, #28]                   // 4-byte Folded Spill
	sub	w9, w8, w9
	cmp	w9, w10
	csel	w19, w10, w9, hi
	cmp	w19, w8
	b.ne	.LBB51_50
// %bb.47:                              //   in Loop: Header=BB51_41 Depth=2
	ldr	x9, [x22, :lo12:req_prgm]
	ldr	x10, [sp, #40]                  // 8-byte Folded Reload
	mov	w19, w8
	add	x9, x9, x25
	str	x10, [x9, #8]
	b	.LBB51_63
.LBB51_48:                              //   in Loop: Header=BB51_41 Depth=2
	ldp	w11, w10, [sp, #28]             // 8-byte Folded Reload
	mul	w9, w27, w8
	cmp	w27, w10
	ldr	w10, [sp, #24]                  // 4-byte Folded Reload
	csel	w11, wzr, w11, eq
	str	w11, [sp, #28]                  // 4-byte Folded Spill
	sub	w10, w10, w9
	and	w10, w10, #0xffff
	csel	w19, w10, w19, eq
	cmp	w19, w8
	b.ne	.LBB51_51
// %bb.49:                              //   in Loop: Header=BB51_41 Depth=2
	ldr	w11, [sp, #36]                  // 4-byte Folded Reload
	ldr	x10, [x22, :lo12:req_prgm]
	mov	w19, w8
	sub	w9, w9, w11
	ldr	x11, [sp, #40]                  // 8-byte Folded Reload
	lsl	w9, w9, #9
	add	x10, x10, x25
	add	x9, x11, x9
	str	x9, [x10, #8]
	b	.LBB51_63
.LBB51_50:                              //   in Loop: Header=BB51_41 Depth=2
	adrp	x8, p_io_data_buf_0
	add	x8, x8, :lo12:p_io_data_buf_0
	b	.LBB51_52
.LBB51_51:                              //   in Loop: Header=BB51_41 Depth=2
	adrp	x8, p_io_data_buf_1
	add	x8, x8, :lo12:p_io_data_buf_1
.LBB51_52:                              //   in Loop: Header=BB51_41 Depth=2
	ldr	x9, [x22, :lo12:req_prgm]
	ldr	x8, [x8]
	add	x9, x9, x25
	str	x8, [x9, #8]
	ldur	w8, [x29, #-12]
	cmn	w8, #1                          // =1
	b.eq	.LBB51_57
// %bb.53:                              //   in Loop: Header=BB51_41 Depth=2
	ldr	x9, [x22, :lo12:req_prgm]
	str	w8, [sp, #52]
	str	w27, [sp, #72]
	add	x0, sp, #48                     // =48
	add	x8, x9, x25
	ldp	x9, x8, [x8, #8]
	mov	w1, #1
	stp	x9, x8, [sp, #56]
	bl	FlashReadPages
	ldr	w8, [sp, #48]
	cmn	w8, #1                          // =1
	b.eq	.LBB51_58
// %bb.54:                              //   in Loop: Header=BB51_41 Depth=2
	ldr	w8, [x26, #8]
	cmp	w27, w8
	b.eq	.LBB51_59
// %bb.55:                              //   in Loop: Header=BB51_41 Depth=2
	adrp	x9, g_sys_ext_data+72
	ldr	w8, [x9, :lo12:g_sys_ext_data+72]
	adrp	x0, .L.str.81
	add	x0, x0, :lo12:.L.str.81
	mov	w2, w27
	add	w8, w8, #1                      // =1
	str	w8, [x9, :lo12:g_sys_ext_data+72]
	ldr	w1, [x26, #8]
	bl	sftl_printk
	ldr	w8, [x26, #8]
	cmp	w27, w8
	b.eq	.LBB51_59
// %bb.56:                              //   in Loop: Header=BB51_41 Depth=2
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlWrite
	mov	w2, #1128
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlWrite
	bl	sftl_printk
	b	.LBB51_59
.LBB51_57:                              //   in Loop: Header=BB51_41 Depth=2
	ldr	x8, [x22, :lo12:req_prgm]
	mov	w1, wzr
	add	x8, x8, x25
	ldr	x0, [x8, #8]
	adrp	x8, c_ftl_nand_byte_pre_page
	ldrh	w2, [x8, :lo12:c_ftl_nand_byte_pre_page]
	bl	memset
	b	.LBB51_59
.LBB51_58:                              //   in Loop: Header=BB51_41 Depth=2
	adrp	x9, g_sys_ext_data+72
	ldr	w8, [x9, :lo12:g_sys_ext_data+72]
	adrp	x0, .L.str.80
	mov	w3, #-1
	add	x0, x0, :lo12:.L.str.80
	add	w8, w8, #1                      // =1
	str	w8, [x9, :lo12:g_sys_ext_data+72]
	ldr	w1, [x26, #8]
	mov	w2, w27
	bl	sftl_printk
.LBB51_59:                              //   in Loop: Header=BB51_41 Depth=2
	ldr	x8, [x22, :lo12:req_prgm]
	cmp	w27, w24
	add	x8, x8, x25
	ldr	x0, [x8, #8]
	b.ne	.LBB51_61
// %bb.60:                              //   in Loop: Header=BB51_41 Depth=2
	ldr	w8, [sp, #28]                   // 4-byte Folded Reload
	ldr	x1, [sp, #40]                   // 8-byte Folded Reload
	lsl	w2, w19, #9
	lsl	w8, w8, #9
	add	x0, x0, x8
	b	.LBB51_62
.LBB51_61:                              //   in Loop: Header=BB51_41 Depth=2
	adrp	x8, c_ftl_nand_sec_pre_page
	ldr	w9, [sp, #36]                   // 4-byte Folded Reload
	ldrh	w8, [x8, :lo12:c_ftl_nand_sec_pre_page]
	lsl	w2, w19, #9
	neg	w9, w9
	madd	w8, w27, w8, w9
	ldr	x9, [sp, #40]                   // 8-byte Folded Reload
	lsl	w8, w8, #9
	add	x1, x9, x8
.LBB51_62:                              //   in Loop: Header=BB51_41 Depth=2
	bl	memcpy
.LBB51_63:                              //   in Loop: Header=BB51_41 Depth=2
	adrp	x9, c_ftl_nand_planes_num
	ldrb	w8, [x28, #6]
	ldrh	w9, [x9, :lo12:c_ftl_nand_planes_num]
	cmp	w9, w8
	b.hi	.LBB51_40
// %bb.64:                              //   in Loop: Header=BB51_41 Depth=2
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlWrite
	mov	w2, #1143
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlWrite
	bl	sftl_printk
	b	.LBB51_40
.LBB51_65:                              //   in Loop: Header=BB51_25 Depth=1
	ldp	w21, w20, [sp, #12]             // 8-byte Folded Reload
	add	w21, w21, w23
                                        // kill: def $w23 killed $w23 killed $x23
	b	.LBB51_67
.LBB51_66:                              //   in Loop: Header=BB51_25 Depth=1
	ldp	w21, w20, [sp, #12]             // 8-byte Folded Reload
	add	w21, w21, w23
	ldr	w23, [sp, #8]                   // 4-byte Folded Reload
.LBB51_67:                              //   in Loop: Header=BB51_25 Depth=1
	ldr	x0, [x22, :lo12:req_prgm]
	mov	w1, w23
	mov	x3, x28
	bl	FtlProgPages
	cmp	w20, w23
	b.hs	.LBB51_24
// %bb.68:                              //   in Loop: Header=BB51_25 Depth=1
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlWrite
	mov	w2, #1152
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlWrite
	bl	sftl_printk
	b	.LBB51_24
.LBB51_69:                              //   in Loop: Header=BB51_25 Depth=1
	mov	x0, x28
	bl	allocate_new_data_superblock
	mov	x23, x28
	ldrh	w8, [x23, #4]
	cbnz	w8, .LBB51_35
	b	.LBB51_32
.LBB51_70:
	bl	__stack_chk_fail
.Lfunc_end51:
	.size	FtlWrite, .Lfunc_end51-FtlWrite
                                        // -- End function
	.globl	FtlVendorPartWrite              // -- Begin function FtlVendorPartWrite
	.p2align	2
	.type	FtlVendorPartWrite,@function
FtlVendorPartWrite:                     // @FtlVendorPartWrite
// %bb.0:
	hint	#25
	sub	sp, sp, #208                    // =208
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	stp	x29, x30, [sp, #112]            // 16-byte Folded Spill
	add	x29, sp, #112                   // =112
	stp	x28, x27, [sp, #128]            // 16-byte Folded Spill
	stp	x26, x25, [sp, #144]            // 16-byte Folded Spill
	stp	x24, x23, [sp, #160]            // 16-byte Folded Spill
	stp	x22, x21, [sp, #176]            // 16-byte Folded Spill
	stp	x20, x19, [sp, #192]            // 16-byte Folded Spill
	stur	x8, [x29, #-8]
	adrp	x8, c_ftl_vendor_part_size
	ldrh	w8, [x8, :lo12:c_ftl_vendor_part_size]
	add	w9, w1, w0
	cmp	w9, w8
	b.ls	.LBB52_2
// %bb.1:
	mov	w0, #-1
	b	.LBB52_10
.LBB52_2:
	mov	w19, w1
	cbz	w1, .LBB52_9
// %bb.3:
	adrp	x8, c_ftl_nand_sec_pre_page_shift
	ldrh	w8, [x8, :lo12:c_ftl_nand_sec_pre_page_shift]
	adrp	x22, gVendorBlkInfo
	mov	w20, w0
	mov	x21, x2
	adrp	x25, p_vendor_region_ppn_table
	adrp	x26, c_ftl_nand_sec_pre_page
	adrp	x27, p_vendor_data_buf
	add	x22, x22, :lo12:gVendorBlkInfo
	lsr	w23, w0, w8
	b	.LBB52_6
.LBB52_4:                               //   in Loop: Header=BB52_6 Depth=1
	adrp	x8, c_ftl_nand_byte_pre_page
	ldr	x0, [x27, :lo12:p_vendor_data_buf]
	ldrh	w2, [x8, :lo12:c_ftl_nand_byte_pre_page]
	mov	w1, wzr
	bl	memset
.LBB52_5:                               //   in Loop: Header=BB52_6 Depth=1
	ldr	x8, [x27, :lo12:p_vendor_data_buf]
	and	x10, x24, #0xffff
	lsl	w9, w28, #7
	and	w28, w24, #0xffff
	lsl	x24, x10, #9
	add	x0, x8, w9, uxtw #2
	mov	x1, x21
	mov	x2, x24
	bl	memcpy
	ldr	x2, [x27, :lo12:p_vendor_data_buf]
	mov	x0, x22
	mov	w1, w23
	bl	FtlMapWritePage
	add	w23, w23, #1                    // =1
	subs	w19, w19, w28
	add	w20, w20, w28
	add	x21, x21, x24
	b.eq	.LBB52_9
.LBB52_6:                               // =>This Inner Loop Header: Depth=1
	ldrh	w9, [x26, :lo12:c_ftl_nand_sec_pre_page]
	ldr	x8, [x25, :lo12:p_vendor_region_ppn_table]
	udiv	w10, w20, w9
	ldr	w8, [x8, w23, uxtw #2]
	msub	w28, w10, w9, w20
	sub	w10, w9, w28
	cmp	w19, w10, uxth
	csel	w24, w19, w10, lo
	cbz	w8, .LBB52_4
// %bb.7:                               //   in Loop: Header=BB52_6 Depth=1
	cmp	w9, w24, uxth
	b.eq	.LBB52_4
// %bb.8:                               //   in Loop: Header=BB52_6 Depth=1
	ldr	x9, [x27, :lo12:p_vendor_data_buf]
	str	w8, [sp, #12]
	add	x8, sp, #40                     // =40
	add	x0, sp, #8                      // =8
	mov	w1, #1
	stp	x9, x8, [sp, #16]
	bl	FlashReadPages
	b	.LBB52_5
.LBB52_9:
	mov	w0, wzr
.LBB52_10:
	adrp	x9, __stack_chk_guard
	ldur	x8, [x29, #-8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB52_12
// %bb.11:
	ldp	x20, x19, [sp, #192]            // 16-byte Folded Reload
	ldp	x22, x21, [sp, #176]            // 16-byte Folded Reload
	ldp	x24, x23, [sp, #160]            // 16-byte Folded Reload
	ldp	x26, x25, [sp, #144]            // 16-byte Folded Reload
	ldp	x28, x27, [sp, #128]            // 16-byte Folded Reload
	ldp	x29, x30, [sp, #112]            // 16-byte Folded Reload
	add	sp, sp, #208                    // =208
	hint	#29
	ret
.LBB52_12:
	bl	__stack_chk_fail
.Lfunc_end52:
	.size	FtlVendorPartWrite, .Lfunc_end52-FtlVendorPartWrite
                                        // -- End function
	.globl	sftl_discard                    // -- Begin function sftl_discard
	.p2align	2
	.type	sftl_discard,@function
sftl_discard:                           // @sftl_discard
// %bb.0:
	hint	#25
	sub	sp, sp, #96                     // =96
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #32]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #48]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #64]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #80]             // 16-byte Folded Spill
	str	x8, [sp, #8]
	adrp	x8, g_MaxLbaSector
	ldr	w9, [x8, :lo12:g_MaxLbaSector]
	add	w10, w1, w0
	mov	w8, w0
	mov	w0, #-1
	cmp	w10, w9
	add	x29, sp, #16                    // =16
	b.hi	.LBB53_19
// %bb.1:
	cmp	w9, w8
	b.ls	.LBB53_19
// %bb.2:
	mov	w19, w1
	cmp	w9, w1
	b.lo	.LBB53_19
// %bb.3:
	cmp	w19, #32                        // =32
	b.lo	.LBB53_18
// %bb.4:
	adrp	x21, c_ftl_nand_sec_pre_page
	ldrh	w9, [x21, :lo12:c_ftl_nand_sec_pre_page]
	udiv	w20, w8, w9
	msub	w8, w20, w9, w8
	ands	w8, w8, #0xffff
	b.eq	.LBB53_6
// %bb.5:
	sub	w8, w9, w8
	cmp	w8, w19
	csel	w8, w8, w19, lo
	add	w20, w20, #1                    // =1
	sub	w19, w19, w8, uxth
.LBB53_6:
	mov	w8, #-1
	cmp	w19, w9
	adrp	x22, gc_discard_updated
	str	w8, [sp]
	b.hs	.LBB53_13
.LBB53_7:
	ldr	w8, [x22, :lo12:gc_discard_updated]
	cmp	w8, #33                         // =33
	b.lo	.LBB53_18
// %bb.8:
	str	wzr, [x22, :lo12:gc_discard_updated]
	bl	FtlWriteDump_data
	adrp	x20, c_ftl_nand_l2pmap_ram_region_num
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	cbz	w8, .LBB53_17
// %bb.9:
	adrp	x19, gL2pMapInfo
	mov	x21, xzr
	mov	x22, xzr
	adrp	x23, p_l2p_ram_map
	add	x19, x19, :lo12:gL2pMapInfo
	b	.LBB53_11
.LBB53_10:                              //   in Loop: Header=BB53_11 Depth=1
	add	x22, x22, #1                    // =1
	cmp	x22, w8, uxth
	add	x21, x21, #16                   // =16
	b.hs	.LBB53_17
.LBB53_11:                              // =>This Inner Loop Header: Depth=1
	ldr	x9, [x23, :lo12:p_l2p_ram_map]
	add	x9, x9, x21
	ldr	w10, [x9, #4]
	tbz	w10, #31, .LBB53_10
// %bb.12:                              //   in Loop: Header=BB53_11 Depth=1
	ldrh	w1, [x9]
	ldr	x2, [x9, #8]
	mov	x0, x19
	bl	FtlMapWritePage
	ldr	x8, [x23, :lo12:p_l2p_ram_map]
	add	x8, x8, x21
	ldr	w9, [x8, #4]
	and	w9, w9, #0x7fffffff
	str	w9, [x8, #4]
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	b	.LBB53_10
.LBB53_13:
	adrp	x23, g_totle_discard_page_count
	adrp	x24, c_ftl_nand_blks_per_die
	adrp	x25, c_ftl_nand_planes_per_die
	b	.LBB53_15
.LBB53_14:                              //   in Loop: Header=BB53_15 Depth=1
	ldrh	w8, [x21, :lo12:c_ftl_nand_sec_pre_page]
	add	w20, w20, #1                    // =1
	sub	w19, w19, w8
	cmp	w19, w8
	b.lo	.LBB53_7
.LBB53_15:                              // =>This Inner Loop Header: Depth=1
	add	x1, sp, #4                      // =4
	mov	w0, w20
	mov	w2, wzr
	bl	log2phys
	ldr	w26, [sp, #4]
	cmn	w26, #1                         // =1
	b.eq	.LBB53_14
// %bb.16:                              //   in Loop: Header=BB53_15 Depth=1
	ldr	w8, [x22, :lo12:gc_discard_updated]
	ldr	w9, [x23, :lo12:g_totle_discard_page_count]
	mov	x1, sp
	mov	w2, #1
	add	w8, w8, #1                      // =1
	add	w9, w9, #1                      // =1
	mov	w0, w20
	str	w8, [x22, :lo12:gc_discard_updated]
	str	w9, [x23, :lo12:g_totle_discard_page_count]
	bl	log2phys
	ldrh	w8, [x24, :lo12:c_ftl_nand_blks_per_die]
	ldrh	w9, [x25, :lo12:c_ftl_nand_planes_per_die]
	ubfx	w10, w26, #10, #16
	udiv	w11, w10, w8
	msub	w8, w11, w8, w10
	udiv	w0, w8, w9
	bl	decrement_vpc_count
	b	.LBB53_14
.LBB53_17:
	bl	FtlVpcTblFlush
.LBB53_18:
	mov	w0, wzr
.LBB53_19:
	adrp	x9, __stack_chk_guard
	ldr	x8, [sp, #8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB53_21
// %bb.20:
	ldp	x20, x19, [sp, #80]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #64]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #48]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	add	sp, sp, #96                     // =96
	hint	#29
	ret
.LBB53_21:
	bl	__stack_chk_fail
.Lfunc_end53:
	.size	sftl_discard, .Lfunc_end53-sftl_discard
                                        // -- End function
	.globl	sftl_read                       // -- Begin function sftl_read
	.p2align	2
	.type	sftl_read,@function
sftl_read:                              // @sftl_read
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	mov	x3, x2
	mov	w2, w1
	mov	w1, w0
	mov	w0, wzr
	mov	x29, sp
	bl	FtlRead
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end54:
	.size	sftl_read, .Lfunc_end54-sftl_read
                                        // -- End function
	.globl	sftl_write                      // -- Begin function sftl_write
	.p2align	2
	.type	sftl_write,@function
sftl_write:                             // @sftl_write
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-48]!           // 16-byte Folded Spill
	str	x21, [sp, #16]                  // 8-byte Folded Spill
	stp	x20, x19, [sp, #32]             // 16-byte Folded Spill
	mov	x29, sp
	mov	x19, x2
	mov	w20, w1
	mov	w21, w0
	bl	FtlWriteToIDB
	mov	w0, wzr
	mov	w1, w21
	mov	w2, w20
	mov	x3, x19
	bl	FtlWrite
	ldp	x20, x19, [sp, #32]             // 16-byte Folded Reload
	ldr	x21, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #48             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end55:
	.size	sftl_write, .Lfunc_end55-sftl_write
                                        // -- End function
	.globl	FtlWriteToIDB                   // -- Begin function FtlWriteToIDB
	.p2align	2
	.type	FtlWriteToIDB,@function
FtlWriteToIDB:                          // @FtlWriteToIDB
// %bb.0:
	hint	#25
	sub	sp, sp, #208                    // =208
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	stp	x29, x30, [sp, #112]            // 16-byte Folded Spill
	add	x29, sp, #112                   // =112
	cmp	w0, #575                        // =575
	stp	x28, x27, [sp, #128]            // 16-byte Folded Spill
	stp	x26, x25, [sp, #144]            // 16-byte Folded Spill
	stp	x24, x23, [sp, #160]            // 16-byte Folded Spill
	stp	x22, x21, [sp, #176]            // 16-byte Folded Spill
	stp	x20, x19, [sp, #192]            // 16-byte Folded Spill
	stur	x8, [x29, #-8]
	b.hi	.LBB56_6
// %bb.1:
	add	w23, w1, w0
	sub	w24, w23, #1                    // =1
	mov	w20, w1
	mov	w21, w0
	cmp	w24, #63                        // =63
	b.ls	.LBB56_6
// %bb.2:
	mov	x19, x2
	cmp	w21, #64                        // =64
	b.ne	.LBB56_12
// %bb.3:
	mov	w0, #262144
	mov	w1, #3265
	mov	w2, #6
	bl	kmalloc_order_trace
	adrp	x25, idb_buf
	str	x0, [x25, :lo12:idb_buf]
	mov	w0, #262144
	mov	w1, #3265
	mov	w2, #6
	bl	kmalloc_order_trace
	adrp	x8, gp_flash_check_buf
	str	x0, [x8, :lo12:gp_flash_check_buf]
	cbz	x0, .LBB56_15
// %bb.4:
	ldr	x22, [x25, :lo12:idb_buf]
	cbz	x22, .LBB56_15
// %bb.5:
	adrp	x8, idb_need_write_back
	mov	w9, #1
	mov	w2, #262144
	mov	x0, x22
	mov	w1, wzr
	strb	w9, [x8, :lo12:idb_need_write_back]
	bl	memset
	b	.LBB56_17
.LBB56_6:
	adrp	x19, idb_need_write_back
	ldrb	w8, [x19, :lo12:idb_need_write_back]
	cmp	w8, #1                          // =1
	b.ne	.LBB56_19
// %bb.7:
	adrp	x20, idb_buf
	ldr	x21, [x20, :lo12:idb_buf]
	mov	w9, #35899
	movk	w9, #64732, lsl #16
	ldr	w8, [x21]
	cmp	w8, w9
	b.ne	.LBB56_52
// %bb.8:
	adrp	x8, g_nand_phy_info+10
	ldrh	w8, [x8, :lo12:g_nand_phy_info+10]
	mov	w9, wzr
	mov	w11, #65150
	mov	w10, #262140
	lsl	w8, w8, #2
.LBB56_9:                               // =>This Inner Loop Header: Depth=1
	sub	w12, w11, #127                  // =127
	ldr	w12, [x21, w12, uxtw #2]
	cbnz	w12, .LBB56_20
// %bb.10:                              //   in Loop: Header=BB56_9 Depth=1
	ldr	w12, [x21, w9, uxtw #2]
	add	w13, w9, #1                     // =1
	sub	w14, w11, #128                  // =128
	cmp	w13, #1, lsl #12                // =4096
	sub	w11, w11, #1                    // =1
	csinc	w9, wzr, w9, hi
	cmp	w14, #1, lsl #12                // =4096
	str	w12, [x21, x10]
	sub	x10, x10, #4                    // =4
	b.hi	.LBB56_9
// %bb.11:
	mov	w22, #512
	b	.LBB56_21
.LBB56_12:
	adrp	x8, idb_need_write_back
	ldrb	w8, [x8, :lo12:idb_need_write_back]
	tbz	w8, #0, .LBB56_19
// %bb.13:
	adrp	x8, idb_buf
	ldr	x22, [x8, :lo12:idb_buf]
	cmp	w21, #63                        // =63
	b.hi	.LBB56_17
// %bb.14:
	mov	w8, #64
	sub	w8, w8, w21
	sub	w20, w20, w8
	lsl	w8, w8, #7
	add	x19, x19, w8, uxtw #2
	b	.LBB56_18
.LBB56_15:
	adrp	x0, .L.str.130
	adrp	x1, .L__func__.FtlWriteToIDB
	add	x0, x0, :lo12:.L.str.130
	add	x1, x1, :lo12:.L__func__.FtlWriteToIDB
	bl	sftl_printk
	adrp	x8, idb_need_write_back
	ldrb	w8, [x8, :lo12:idb_need_write_back]
	cmp	w8, #1                          // =1
	b.ne	.LBB56_19
// %bb.16:
	ldr	x22, [x25, :lo12:idb_buf]
.LBB56_17:
	lsl	w8, w21, #7
	sub	w8, w8, #2, lsl #12             // =8192
	add	x22, x22, w8, uxtw #2
.LBB56_18:
	mov	w8, #64960
	movk	w8, #127, lsl #16
	add	w8, w23, w8
	cmp	w24, #575                       // =575
	csel	w8, w8, wzr, hi
	sub	w8, w20, w8
	lsl	w2, w8, #9
	mov	x0, x22
	mov	x1, x19
	bl	memcpy
.LBB56_19:
	mov	w22, wzr
	b	.LBB56_54
.LBB56_20:
	lsr	w22, w11, #7
.LBB56_21:
	add	w9, w22, #4                     // =4
	and	w8, w8, #0xffff
	udiv	w27, w9, w8
	add	w26, w27, #1                    // =1
	adrp	x0, .L.str.132
	add	x0, x0, :lo12:.L.str.132
	mov	w1, w22
	mov	w2, w26
	bl	sftl_printk
	cmp	w27, #7                         // =7
	b.hi	.LBB56_52
// %bb.22:
	mov	w9, wzr
	lsl	w25, w22, #7
	adrp	x20, g_nand_ops+8
	mov	w8, w26
	stp	w22, wzr, [sp, #20]             // 8-byte Folded Spill
	str	w26, [sp, #36]                  // 4-byte Folded Spill
	str	x25, [sp, #8]                   // 8-byte Folded Spill
	b	.LBB56_25
.LBB56_23:                              //   in Loop: Header=BB56_25 Depth=1
	ldr	w8, [sp, #24]                   // 4-byte Folded Reload
	add	w8, w8, #1                      // =1
	cmp	w8, #5                          // =5
	str	w8, [sp, #24]                   // 4-byte Folded Spill
	b.hi	.LBB56_56
.LBB56_24:                              //   in Loop: Header=BB56_25 Depth=1
	ldp	w9, w26, [sp, #32]              // 8-byte Folded Reload
	add	w8, w9, w26
	cmp	w8, #9                          // =9
	b.hs	.LBB56_51
.LBB56_25:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB56_26 Depth 2
                                        //     Child Loop BB56_33 Depth 2
                                        //     Child Loop BB56_42 Depth 2
                                        //     Child Loop BB56_48 Depth 2
	adrp	x10, gp_flash_check_buf
	ldr	x0, [x10, :lo12:gp_flash_check_buf]
	mov	w2, #512
	mov	w1, wzr
	mov	w23, w9
	str	w8, [sp, #32]                   // 4-byte Folded Spill
	bl	memset
	adrp	x8, g_nand_phy_info+10
	ldrh	w19, [x8, :lo12:g_nand_phy_info+10]
	adrp	x8, g_nand_ops+32
	add	x8, x8, :lo12:g_nand_ops+32
	ldp	x28, x25, [x8]
	mov	w22, wzr
	str	w23, [sp, #28]                  // 4-byte Folded Spill
	mul	w23, w23, w19
	mov	w24, #-1
.LBB56_26:                              //   Parent Loop BB56_25 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldr	x8, [x20, :lo12:g_nand_ops+8]
	madd	w1, w22, w19, w23
	mov	w0, wzr
	blr	x8
	add	w24, w24, #1                    // =1
	cmp	w24, w27
	add	w22, w22, #1                    // =1
	b.ne	.LBB56_26
// %bb.27:                              //   in Loop: Header=BB56_25 Depth=1
	cmp	x25, #0                         // =0
	cset	w8, ne
	cmp	x28, #0                         // =0
	cset	w9, ne
	and	w22, w9, w8
	cmp	w22, #1                         // =1
	b.ne	.LBB56_29
// %bb.28:                              //   in Loop: Header=BB56_25 Depth=1
	adrp	x8, gp_flash_check_buf
	ldr	x24, [x8, :lo12:gp_flash_check_buf]
	mov	w2, #1016
	mov	w1, wzr
	add	x0, x24, #8                     // =8
	bl	memset
	mov	w8, #18766
	movk	w8, #17998, lsl #16
	str	w8, [x24]
	mov	w8, #12
	str	w8, [x24, #4]
	mov	w8, #1024
	str	wzr, [x24, #12]
	strh	w8, [x24, #16]
	adrp	x8, g_nand_phy_info+10
	ldrh	w8, [x8, :lo12:g_nand_phy_info+10]
	mov	w9, #4096
	str	w9, [x24, #20]
	mov	w9, #47602
	movk	w9, #65230, lsl #16
	mov	w10, #17225
	add	w9, w9, w8, uxtb
	movk	w10, #44697, lsl #16
	eor	w9, w9, w10
	lsl	w10, w9, #5
	strh	w8, [x24, #18]
	add	w8, w10, w8, lsr #8
	add	w8, w8, w9, lsr #2
	eor	w8, w8, w9
	lsr	w9, w8, #2
	add	w9, w9, w8, lsl #5
	eor	w8, w9, w8
	mov	w9, #16
	bfi	w9, w8, #5, #27
	add	w9, w9, w8, lsr #2
	eor	w8, w9, w8
	lsr	w9, w8, #2
	add	w9, w9, w8, lsl #5
	eor	w8, w9, w8
	lsr	w9, w8, #2
	add	w9, w9, w8, lsl #5
	eor	w8, w9, w8
	str	w8, [x24, #8]
	mul	w19, w26, w19
	cbnz	w19, .LBB56_30
	b	.LBB56_38
.LBB56_29:                              //   in Loop: Header=BB56_25 Depth=1
	mov	x24, xzr
	mul	w19, w26, w19
	cbz	w19, .LBB56_38
.LBB56_30:                              //   in Loop: Header=BB56_25 Depth=1
	cmp	x28, #0                         // =0
	cset	w8, eq
	cmp	x25, #0                         // =0
	cset	w9, eq
	mov	w26, wzr
	orr	w28, w8, w9
	mov	x25, x21
	b	.LBB56_33
.LBB56_31:                              //   in Loop: Header=BB56_33 Depth=2
	adrp	x26, g_nand_ops+16
	add	x26, x26, :lo12:g_nand_ops+16
	ldr	x8, [x26, #16]
	mov	w0, #70
	blr	x8
	ldr	x8, [x26]
	add	x3, sp, #40                     // =40
	mov	w0, wzr
	mov	w1, w23
	mov	x2, x24
	blr	x8
	adrp	x8, g_nand_phy_info+22
	ldrb	w8, [x8, :lo12:g_nand_phy_info+22]
	ldr	x9, [x26, #16]
	mov	w26, w0
	mov	w0, w8
	blr	x9
	cmn	w26, #1                         // =1
	b.eq	.LBB56_38
// %bb.32:                              //   in Loop: Header=BB56_33 Depth=2
	mov	w26, #1
	cmp	w26, w19
	b.eq	.LBB56_38
.LBB56_33:                              //   Parent Loop BB56_25 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	tbz	w22, #0, .LBB56_35
// %bb.34:                              //   in Loop: Header=BB56_33 Depth=2
	cmp	w26, #0                         // =0
	cset	w9, ne
	add	w10, w26, #1                    // =1
	orr	w9, w9, w28
	mov	w8, #61424
	stp	w10, w8, [sp, #40]
	tbz	w9, #0, .LBB56_31
	b	.LBB56_36
.LBB56_35:                              //   in Loop: Header=BB56_33 Depth=2
	lsl	w9, w26, #2
	mov	w8, #61424
	stp	w9, w8, [sp, #40]
.LBB56_36:                              //   in Loop: Header=BB56_33 Depth=2
	adrp	x8, g_nand_ops+16
	ldr	x8, [x8, :lo12:g_nand_ops+16]
	add	w1, w26, w23
	add	x3, sp, #40                     // =40
	mov	w0, wzr
	mov	x2, x25
	blr	x8
	cmn	w0, #1                          // =1
	b.eq	.LBB56_38
// %bb.37:                              //   in Loop: Header=BB56_33 Depth=2
	add	x25, x25, #2048                 // =2048
	add	w26, w26, #1                    // =1
	cmp	w26, w19
	b.ne	.LBB56_33
.LBB56_38:                              //   in Loop: Header=BB56_25 Depth=1
	adrp	x8, g_nand_phy_info+10
	add	x8, x8, :lo12:g_nand_phy_info+10
	ldrh	w8, [x8]
	ldr	w9, [sp, #36]                   // 4-byte Folded Reload
	mul	w22, w9, w8
	cbz	w22, .LBB56_46
// %bb.39:                              //   in Loop: Header=BB56_25 Depth=1
	adrp	x10, g_nand_ops+32
	adrp	x9, gp_flash_check_buf
	add	x10, x10, :lo12:g_nand_ops+32
	adrp	x11, g_nand_phy_info+10
	ldr	x23, [x9, :lo12:gp_flash_check_buf]
	ldp	x10, x9, [x10]
	add	x11, x11, :lo12:g_nand_phy_info+10
	ldrh	w24, [x11, #4]
	ldr	w11, [sp, #28]                  // 4-byte Folded Reload
	cmp	x9, #0                          // =0
	mov	w26, wzr
	mul	w25, w11, w8
	cset	w8, eq
	cmp	x10, #0                         // =0
	cset	w9, eq
	orr	w19, w8, w9
	b	.LBB56_42
.LBB56_40:                              //   in Loop: Header=BB56_42 Depth=2
	adrp	x28, g_nand_ops+24
	add	x28, x28, :lo12:g_nand_ops+24
	ldr	x8, [x28, #8]
	mov	w0, #70
	blr	x8
	ldr	x8, [x28, #16]
	mov	w0, #2
	blr	x8
	ldr	x8, [x28]
	add	x3, sp, #40                     // =40
	mov	w0, wzr
	mov	w1, w25
	mov	x2, x23
	blr	x8
	ldr	x8, [x28, #16]
	mov	w0, w24
	blr	x8
	adrp	x9, g_nand_phy_info+22
	ldr	x8, [x28, #8]
	ldrb	w0, [x9, :lo12:g_nand_phy_info+22]
	blr	x8
	ldr	w8, [x23]
	mov	w9, #18766
	movk	w9, #17998, lsl #16
	cmp	w8, w9
	b.ne	.LBB56_46
.LBB56_41:                              //   in Loop: Header=BB56_42 Depth=2
	add	w26, w26, #1                    // =1
	cmp	w26, w22
	b.eq	.LBB56_46
.LBB56_42:                              //   Parent Loop BB56_25 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	cmp	w26, #0                         // =0
	cset	w8, ne
	orr	w8, w19, w8
	tbz	w8, #0, .LBB56_40
// %bb.43:                              //   in Loop: Header=BB56_42 Depth=2
	adrp	x8, g_nand_ops+24
	ldr	x8, [x8, :lo12:g_nand_ops+24]
	add	w1, w26, w25
	add	x3, sp, #40                     // =40
	mov	w0, wzr
	mov	x2, x23
	blr	x8
	cmn	w0, #1                          // =1
	b.eq	.LBB56_46
// %bb.44:                              //   in Loop: Header=BB56_42 Depth=2
	ldr	w8, [sp, #44]
	mov	w9, #61424
	cmp	w8, w9
	b.ne	.LBB56_46
// %bb.45:                              //   in Loop: Header=BB56_42 Depth=2
	add	x23, x23, #2048                 // =2048
	b	.LBB56_41
.LBB56_46:                              //   in Loop: Header=BB56_25 Depth=1
	ldr	w22, [sp, #20]                  // 4-byte Folded Reload
	ldr	x25, [sp, #8]                   // 8-byte Folded Reload
	cbz	w22, .LBB56_23
// %bb.47:                              //   in Loop: Header=BB56_25 Depth=1
	adrp	x8, gp_flash_check_buf
	ldr	x24, [x8, :lo12:gp_flash_check_buf]
	mov	x23, xzr
.LBB56_48:                              //   Parent Loop BB56_25 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	lsl	x19, x23, #2
	ldr	w8, [x24, x19]
	ldr	w9, [x21, x19]
	cmp	w8, w9
	b.ne	.LBB56_50
// %bb.49:                              //   in Loop: Header=BB56_48 Depth=2
	add	x23, x23, #1                    // =1
	cmp	x25, x23
	b.ne	.LBB56_48
	b	.LBB56_23
.LBB56_50:                              //   in Loop: Header=BB56_25 Depth=1
	mov	w2, #512
	mov	x0, x24
	mov	w1, wzr
	bl	memset
	ldr	w2, [x24, x19]
	ldr	w3, [x21, x19]
	ldr	w19, [sp, #28]                  // 4-byte Folded Reload
	adrp	x0, .L.str.133
	add	x0, x0, :lo12:.L.str.133
	mov	w4, w23
	mov	w1, w19
	bl	sftl_printk
	adrp	x8, g_nand_phy_info+10
	ldrh	w8, [x8, :lo12:g_nand_phy_info+10]
	ldr	x9, [x20, :lo12:g_nand_ops+8]
	mov	w0, wzr
	mul	w1, w19, w8
	blr	x9
	b	.LBB56_24
.LBB56_51:
	ldr	w8, [sp, #24]                   // 4-byte Folded Reload
	adrp	x19, idb_need_write_back
	adrp	x20, idb_buf
	cbnz	w8, .LBB56_53
.LBB56_52:
	mov	w22, #-1
.LBB56_53:
	ldr	x0, [x20, :lo12:idb_buf]
	strb	wzr, [x19, :lo12:idb_need_write_back]
	bl	kfree
	adrp	x8, gp_flash_check_buf
	ldr	x0, [x8, :lo12:gp_flash_check_buf]
	bl	kfree
.LBB56_54:
	adrp	x9, __stack_chk_guard
	ldur	x8, [x29, #-8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB56_57
// %bb.55:
	mov	w0, w22
	ldp	x20, x19, [sp, #192]            // 16-byte Folded Reload
	ldp	x22, x21, [sp, #176]            // 16-byte Folded Reload
	ldp	x24, x23, [sp, #160]            // 16-byte Folded Reload
	ldp	x26, x25, [sp, #144]            // 16-byte Folded Reload
	ldp	x28, x27, [sp, #128]            // 16-byte Folded Reload
	ldp	x29, x30, [sp, #112]            // 16-byte Folded Reload
	add	sp, sp, #208                    // =208
	hint	#29
	ret
.LBB56_56:
	adrp	x19, idb_need_write_back
	adrp	x20, idb_buf
	b	.LBB56_53
.LBB56_57:
	bl	__stack_chk_fail
.Lfunc_end56:
	.size	FtlWriteToIDB, .Lfunc_end56-FtlWriteToIDB
                                        // -- End function
	.globl	sftl_vendor_read                // -- Begin function sftl_vendor_read
	.p2align	2
	.type	sftl_vendor_read,@function
sftl_vendor_read:                       // @sftl_vendor_read
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	add	w0, w0, #256                    // =256
	mov	x29, sp
	bl	FtlVendorPartRead
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end57:
	.size	sftl_vendor_read, .Lfunc_end57-sftl_vendor_read
                                        // -- End function
	.globl	sftl_vendor_write               // -- Begin function sftl_vendor_write
	.p2align	2
	.type	sftl_vendor_write,@function
sftl_vendor_write:                      // @sftl_vendor_write
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	add	w0, w0, #256                    // =256
	mov	x29, sp
	bl	FtlVendorPartWrite
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end58:
	.size	sftl_vendor_write, .Lfunc_end58-sftl_vendor_write
                                        // -- End function
	.globl	sftl_get_density                // -- Begin function sftl_get_density
	.p2align	2
	.type	sftl_get_density,@function
sftl_get_density:                       // @sftl_get_density
// %bb.0:
	hint	#25
	adrp	x8, g_MaxLbaSector
	ldr	w0, [x8, :lo12:g_MaxLbaSector]
	hint	#29
	ret
.Lfunc_end59:
	.size	sftl_get_density, .Lfunc_end59-sftl_get_density
                                        // -- End function
	.globl	FtlBbtInfoPrint                 // -- Begin function FtlBbtInfoPrint
	.p2align	2
	.type	FtlBbtInfoPrint,@function
FtlBbtInfoPrint:                        // @FtlBbtInfoPrint
// %bb.0:
	hint	#25
	hint	#29
	ret
.Lfunc_end60:
	.size	FtlBbtInfoPrint, .Lfunc_end60-FtlBbtInfoPrint
                                        // -- End function
	.globl	FtlBbt2Bitmap                   // -- Begin function FtlBbt2Bitmap
	.p2align	2
	.type	FtlBbt2Bitmap,@function
FtlBbt2Bitmap:                          // @FtlBbt2Bitmap
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-80]!           // 16-byte Folded Spill
	adrp	x8, c_ftl_nand_bbm_buf_size
	ldrh	w8, [x8, :lo12:c_ftl_nand_bbm_buf_size]
	stp	x20, x19, [sp, #64]             // 16-byte Folded Spill
	mov	x19, x1
	mov	x20, x0
	lsl	x2, x8, #2
	mov	x0, x1
	mov	w1, wzr
	stp	x26, x25, [sp, #16]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #32]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #48]             // 16-byte Folded Spill
	mov	x29, sp
	bl	memset
	adrp	x21, .L.str.78
	adrp	x22, .L__func__.FtlBbt2Bitmap
	mov	x23, xzr
	mov	w24, #65535
	adrp	x25, c_ftl_nand_blks_per_die
	mov	w26, #1
	add	x21, x21, :lo12:.L.str.78
	add	x22, x22, :lo12:.L__func__.FtlBbt2Bitmap
	b	.LBB61_2
.LBB61_1:                               //   in Loop: Header=BB61_2 Depth=1
	lsr	w9, w8, #3
	and	x9, x9, #0x1ffc
	ldr	w10, [x19, x9]
	lsl	w8, w26, w8
	add	x23, x23, #2                    // =2
	cmp	x23, #1024                      // =1024
	orr	w8, w8, w10
	str	w8, [x19, x9]
	b.eq	.LBB61_5
.LBB61_2:                               // =>This Inner Loop Header: Depth=1
	ldrh	w8, [x20, x23]
	cmp	w8, w24
	b.eq	.LBB61_5
// %bb.3:                               //   in Loop: Header=BB61_2 Depth=1
	ldrh	w9, [x25, :lo12:c_ftl_nand_blks_per_die]
	cmp	w8, w9
	b.lo	.LBB61_1
// %bb.4:                               //   in Loop: Header=BB61_2 Depth=1
	mov	w2, #74
	mov	x0, x21
	mov	x1, x22
	bl	sftl_printk
	ldrh	w8, [x20, x23]
	b	.LBB61_1
.LBB61_5:
	ldp	x20, x19, [sp, #64]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #48]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #32]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #16]             // 16-byte Folded Reload
	ldp	x29, x30, [sp], #80             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end61:
	.size	FtlBbt2Bitmap, .Lfunc_end61-FtlBbt2Bitmap
                                        // -- End function
	.globl	FtlBbtMemInit                   // -- Begin function FtlBbtMemInit
	.p2align	2
	.type	FtlBbtMemInit,@function
FtlBbtMemInit:                          // @FtlBbtMemInit
// %bb.0:
	hint	#25
	adrp	x8, gBbtInfo
	add	x8, x8, :lo12:gBbtInfo
	mov	w9, #65535
	mov	x10, #-1
	strh	w9, [x8]
	strh	wzr, [x8, #6]
	stur	x10, [x8, #12]
	stur	x10, [x8, #20]
	hint	#29
	ret
.Lfunc_end62:
	.size	FtlBbtMemInit, .Lfunc_end62-FtlBbtMemInit
                                        // -- End function
	.globl	FtlBbtCalcTotleCnt              // -- Begin function FtlBbtCalcTotleCnt
	.p2align	2
	.type	FtlBbtCalcTotleCnt,@function
FtlBbtCalcTotleCnt:                     // @FtlBbtCalcTotleCnt
// %bb.0:
	hint	#25
	adrp	x8, c_ftl_nand_blks_per_die
	adrp	x9, c_ftl_nand_die_num
	ldrh	w8, [x8, :lo12:c_ftl_nand_blks_per_die]
	ldrh	w11, [x9, :lo12:c_ftl_nand_die_num]
	mul	w9, w11, w8
	subs	w10, w9, #1                     // =1
	b.lt	.LBB63_3
// %bb.1:
	cmp	w9, #2                          // =2
	b.hs	.LBB63_4
// %bb.2:
	mov	w0, wzr
	mov	w10, wzr
	b	.LBB63_10
.LBB63_3:
	mov	w0, wzr
	hint	#29
	ret
.LBB63_4:
	mneg	w11, w11, w8
	tst	w11, #0xffff
	mov	w0, wzr
	b.eq	.LBB63_9
// %bb.5:
	lsr	w11, w10, #16
	mov	w10, w0
	cbnz	w11, .LBB63_10
// %bb.6:
	adrp	x14, gBbtInfo+32
	mov	w13, wzr
	mov	w12, wzr
	and	w10, w9, #0xfffffffe
	add	x14, x14, :lo12:gBbtInfo+32
.LBB63_7:                               // =>This Inner Loop Header: Depth=1
	orr	w15, w13, #0x1
	and	w16, w13, #0xffff
	and	w17, w8, #0xffff
	and	w18, w15, #0xffff
	udiv	w16, w16, w17
	udiv	w17, w18, w17
	msub	w18, w16, w8, w13
	ldr	x16, [x14, w16, uxtw #3]
	msub	w15, w17, w8, w15
	ldr	x17, [x14, w17, uxtw #3]
	lsr	w0, w18, #3
	lsr	w1, w15, #3
	and	x0, x0, #0x1ffc
	and	x1, x1, #0x1ffc
	ldr	w16, [x16, x0]
	ldr	w17, [x17, x1]
	add	w13, w13, #2                    // =2
	cmp	w10, w13
	lsr	w16, w16, w18
	lsr	w15, w17, w15
	and	w16, w16, #0x1
	and	w15, w15, #0x1
	add	w11, w11, w16
	add	w12, w12, w15
	b.ne	.LBB63_7
// %bb.8:
	cmp	w9, w10
	add	w0, w12, w11
	b.ne	.LBB63_10
	b	.LBB63_12
.LBB63_9:
	mov	w10, w0
.LBB63_10:
	adrp	x11, gBbtInfo
	add	x11, x11, :lo12:gBbtInfo
.LBB63_11:                              // =>This Inner Loop Header: Depth=1
	and	w12, w8, #0xffff
	and	w13, w10, #0xffff
	udiv	w12, w13, w12
	add	x13, x11, w12, uxtw #3
	ldr	x13, [x13, #32]
	msub	w12, w12, w8, w10
	lsr	w14, w12, #3
	and	x14, x14, #0x1ffc
	ldr	w13, [x13, x14]
	add	w10, w10, #1                    // =1
	cmp	w9, w10, uxth
	lsr	w12, w13, w12
	and	w12, w12, #0x1
	add	w0, w0, w12
	b.gt	.LBB63_11
.LBB63_12:
	hint	#29
	ret
.Lfunc_end63:
	.size	FtlBbtCalcTotleCnt, .Lfunc_end63-FtlBbtCalcTotleCnt
                                        // -- End function
	.globl	FtlGetLastWrittenPage           // -- Begin function FtlGetLastWrittenPage
	.p2align	2
	.type	FtlGetLastWrittenPage,@function
FtlGetLastWrittenPage:                  // @FtlGetLastWrittenPage
// %bb.0:
	hint	#25
	sub	sp, sp, #160                    // =160
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	adrp	x9, c_ftl_nand_page_pre_slc_blk
	adrp	x10, c_ftl_nand_page_pre_blk
	stp	x29, x30, [sp, #112]            // 16-byte Folded Spill
	add	x29, sp, #112                   // =112
	add	x9, x9, :lo12:c_ftl_nand_page_pre_slc_blk
	cmp	w1, #1                          // =1
	add	x10, x10, :lo12:c_ftl_nand_page_pre_blk
	stp	x22, x21, [sp, #128]            // 16-byte Folded Spill
	stp	x20, x19, [sp, #144]            // 16-byte Folded Spill
	stur	x8, [x29, #-8]
	adrp	x8, sftl_temp_buf
	csel	x9, x9, x10, eq
	ldr	x8, [x8, :lo12:sftl_temp_buf]
	ldrh	w9, [x9]
	add	x10, sp, #40                    // =40
	lsl	w20, w0, #10
	stp	x8, x10, [sp, #16]
	sub	w8, w9, #1                      // =1
	sxth	w19, w8
	orr	w8, w20, w19
	add	x0, sp, #8                      // =8
	mov	w1, #1
	str	w8, [sp, #12]
	bl	FlashReadPages
	ldr	w8, [sp, #40]
	cmn	w8, #1                          // =1
	b.ne	.LBB64_4
// %bb.1:
	tbnz	w19, #31, .LBB64_4
// %bb.2:
	mov	w21, wzr
.LBB64_3:                               // =>This Inner Loop Header: Depth=1
	sxth	w8, w21
	add	w8, w8, w19, sxth
	cmp	w8, #0                          // =0
	cinc	w8, w8, lt
	asr	w22, w8, #1
	orr	w8, w22, w20
	add	x0, sp, #8                      // =8
	mov	w1, #1
	str	w8, [sp, #12]
	bl	FlashReadPages
	ldp	w8, w9, [sp, #40]
	ldr	w10, [sp, #8]
	and	w8, w8, w9
	cmn	w8, #1                          // =1
	cset	w8, ne
	cmn	w10, #1                         // =1
	cset	w9, eq
	orr	w8, w8, w9
	sub	w9, w22, #1                     // =1
	cmp	w8, #0                          // =0
	csel	w8, w19, w9, ne
	csinc	w9, w21, w22, eq
	sxth	w19, w8
	sxth	w21, w9
	cmp	w21, w19
	b.le	.LBB64_3
.LBB64_4:
	adrp	x9, __stack_chk_guard
	ldur	x8, [x29, #-8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB64_6
// %bb.5:
	mov	w0, w19
	ldp	x20, x19, [sp, #144]            // 16-byte Folded Reload
	ldp	x22, x21, [sp, #128]            // 16-byte Folded Reload
	ldp	x29, x30, [sp, #112]            // 16-byte Folded Reload
	add	sp, sp, #160                    // =160
	hint	#29
	ret
.LBB64_6:
	bl	__stack_chk_fail
.Lfunc_end64:
	.size	FtlGetLastWrittenPage, .Lfunc_end64-FtlGetLastWrittenPage
                                        // -- End function
	.globl	FtlLoadFactoryBbt               // -- Begin function FtlLoadFactoryBbt
	.p2align	2
	.type	FtlLoadFactoryBbt,@function
FtlLoadFactoryBbt:                      // @FtlLoadFactoryBbt
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-96]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #80]             // 16-byte Folded Spill
	adrp	x8, p_sys_data_buf
	adrp	x9, p_sys_spare_buf
	adrp	x20, c_ftl_nand_die_num
	stp	x22, x21, [sp, #64]             // 16-byte Folded Spill
	ldr	x8, [x8, :lo12:p_sys_data_buf]
	ldr	x21, [x9, :lo12:p_sys_spare_buf]
	ldrh	w9, [x20, :lo12:c_ftl_nand_die_num]
	adrp	x10, req_sys+8
	add	x10, x10, :lo12:req_sys+8
	str	x27, [sp, #16]                  // 8-byte Folded Spill
	stp	x26, x25, [sp, #32]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #48]             // 16-byte Folded Spill
	mov	x29, sp
	stp	x8, x21, [x10]
	cbz	w9, .LBB65_8
// %bb.1:
	adrp	x23, gBbtInfo
	adrp	x19, req_sys
	mov	x22, xzr
	add	x23, x23, :lo12:gBbtInfo
	mov	w24, #65535
	adrp	x25, c_ftl_nand_blks_per_die
	add	x19, x19, :lo12:req_sys
	b	.LBB65_3
.LBB65_2:                               //   in Loop: Header=BB65_3 Depth=1
	ldrh	w8, [x20, :lo12:c_ftl_nand_die_num]
	add	x22, x22, #1                    // =1
	cmp	x22, x8
	b.hs	.LBB65_8
.LBB65_3:                               // =>This Loop Header: Depth=1
                                        //     Child Loop BB65_5 Depth 2
	ldrh	w8, [x25, :lo12:c_ftl_nand_blks_per_die]
	add	x26, x23, x22, lsl #1
	strh	w24, [x26, #12]!
	sub	w9, w8, #1                      // =1
	and	w27, w9, #0xffff
	sub	w9, w8, #16                     // =16
	cmp	w9, w27
	b.lt	.LBB65_5
	b	.LBB65_2
.LBB65_4:                               //   in Loop: Header=BB65_5 Depth=2
	ldrh	w8, [x25, :lo12:c_ftl_nand_blks_per_die]
	sub	w9, w27, #1                     // =1
	and	w27, w9, #0xffff
	sub	w9, w8, #16                     // =16
	cmp	w9, w27
	b.ge	.LBB65_2
.LBB65_5:                               //   Parent Loop BB65_3 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	madd	w8, w8, w22, w27
	lsl	w8, w8, #10
	mov	w1, #1
	mov	x0, x19
	str	w8, [x19, #4]
	bl	FlashReadPages
	ldr	w8, [x19]
	cmn	w8, #1                          // =1
	b.eq	.LBB65_4
// %bb.6:                               //   in Loop: Header=BB65_5 Depth=2
	ldrsh	w8, [x21]
	cmn	w8, #3872                       // =3872
	b.ne	.LBB65_4
// %bb.7:                               //   in Loop: Header=BB65_3 Depth=1
	strh	w27, [x26]
	b	.LBB65_2
.LBB65_8:
	ldp	x20, x19, [sp, #80]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #64]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #48]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #32]             // 16-byte Folded Reload
	ldr	x27, [sp, #16]                  // 8-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #96             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end65:
	.size	FtlLoadFactoryBbt, .Lfunc_end65-FtlLoadFactoryBbt
                                        // -- End function
	.globl	FlashGetBadBlockList            // -- Begin function FlashGetBadBlockList
	.p2align	2
	.type	FlashGetBadBlockList,@function
FlashGetBadBlockList:                   // @FlashGetBadBlockList
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #16]             // 16-byte Folded Spill
	mov	x20, #-1
	stp	x20, x20, [x0, #240]
	stp	x20, x20, [x0, #224]
	stp	x20, x20, [x0, #208]
	stp	x20, x20, [x0, #192]
	stp	x20, x20, [x0, #176]
	stp	x20, x20, [x0, #160]
	stp	x20, x20, [x0, #144]
	stp	x20, x20, [x0, #128]
	stp	x20, x20, [x0, #112]
	stp	x20, x20, [x0, #96]
	stp	x20, x20, [x0, #80]
	stp	x20, x20, [x0, #64]
	stp	x20, x20, [x0, #48]
	stp	x20, x20, [x0, #32]
	stp	x20, x20, [x0, #16]
	stp	x20, x20, [x0]
	adrp	x8, g_nand_ops
	ldr	x8, [x8, :lo12:g_nand_ops]
	mov	x29, sp
	mov	x19, x0
	blr	x8
	and	w8, w0, #0xffff
	cmp	w8, #51                         // =51
	b.lo	.LBB66_2
// %bb.1:
	mov	w0, wzr
	stp	x20, x20, [x19, #240]
	stp	x20, x20, [x19, #224]
	stp	x20, x20, [x19, #208]
	stp	x20, x20, [x19, #192]
	stp	x20, x20, [x19, #176]
	stp	x20, x20, [x19, #160]
	stp	x20, x20, [x19, #144]
	stp	x20, x20, [x19, #128]
	stp	x20, x20, [x19, #112]
	stp	x20, x20, [x19, #96]
	stp	x20, x20, [x19, #80]
	stp	x20, x20, [x19, #64]
	stp	x20, x20, [x19, #48]
	stp	x20, x20, [x19, #32]
	stp	x20, x20, [x19, #16]
	stp	x20, x20, [x19]
	b	.LBB66_11
.LBB66_2:
	adrp	x8, g_nand_phy_info+14
	ldrh	w8, [x8, :lo12:g_nand_phy_info+14]
	cmp	w8, #4                          // =4
	b.ne	.LBB66_11
// %bb.3:
	tst	w0, #0xffff
	b.eq	.LBB66_11
// %bb.4:
	and	w8, w0, #0xffff
	cmp	w8, #2                          // =2
	b.hs	.LBB66_6
// %bb.5:
	mov	x9, xzr
	b	.LBB66_9
.LBB66_6:
	and	x9, x8, #0xfffe
	add	x10, x19, #2                    // =2
	mov	x11, x9
.LBB66_7:                               // =>This Inner Loop Header: Depth=1
	ldurh	w12, [x10, #-2]
	ldrh	w13, [x10]
	subs	x11, x11, #2                    // =2
	lsr	w12, w12, #1
	lsr	w13, w13, #1
	sturh	w12, [x10, #-2]
	strh	w13, [x10], #4
	b.ne	.LBB66_7
// %bb.8:
	cmp	x9, x8
	b.eq	.LBB66_11
.LBB66_9:
	add	x10, x19, x9, lsl #1
	sub	x8, x8, x9
.LBB66_10:                              // =>This Inner Loop Header: Depth=1
	ldrh	w9, [x10]
	subs	x8, x8, #1                      // =1
	lsr	w9, w9, #1
	strh	w9, [x10], #2
	b.ne	.LBB66_10
.LBB66_11:
	ldp	x20, x19, [sp, #16]             // 16-byte Folded Reload
	and	w0, w0, #0xffff
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end66:
	.size	FlashGetBadBlockList, .Lfunc_end66-FlashGetBadBlockList
                                        // -- End function
	.globl	FlashTestBlk                    // -- Begin function FlashTestBlk
	.p2align	2
	.type	FlashTestBlk,@function
FlashTestBlk:                           // @FlashTestBlk
// %bb.0:
	hint	#25
	sub	sp, sp, #144                    // =144
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	and	w9, w0, #0xffff
	stp	x29, x30, [sp, #112]            // 16-byte Folded Spill
	add	x29, sp, #112                   // =112
	cmp	w9, #12                         // =12
	stp	x20, x19, [sp, #128]            // 16-byte Folded Spill
	stur	x8, [x29, #-8]
	b.hs	.LBB67_2
// %bb.1:
	mov	w19, wzr
	b	.LBB67_6
.LBB67_2:
	adrp	x8, sftl_temp_buf
	mov	x10, #42405
	mov	x11, #23130
	ldr	x8, [x8, :lo12:sftl_temp_buf]
	movk	x10, #42405, lsl #16
	movk	x11, #23130, lsl #16
	movk	x10, #42405, lsl #32
	movk	x11, #23130, lsl #32
	add	x9, sp, #40                     // =40
	movk	x10, #42405, lsl #48
	movk	x11, #23130, lsl #48
	ubfiz	w20, w0, #10, #16
	add	x0, sp, #8                      // =8
	mov	w2, #1
	stp	x10, x10, [sp, #40]
	stp	x10, x10, [sp, #56]
	stp	x8, x9, [sp, #16]
	str	x11, [x8]
	str	w20, [sp, #12]
	bl	FlashEraseBlocks
	add	x0, sp, #8                      // =8
	mov	w1, #1
	mov	w2, #1
	bl	FlashProgPages
	ldr	w8, [sp, #8]
	cbz	w8, .LBB67_4
// %bb.3:
	mov	w19, #-1
	b	.LBB67_5
.LBB67_4:
	orr	w8, w20, #0x1
	add	x0, sp, #8                      // =8
	mov	w1, #1
	mov	w2, #1
	str	w8, [sp, #12]
	bl	FlashProgPages
	ldr	w8, [sp, #8]
	cmp	w8, #0                          // =0
	csetm	w19, ne
.LBB67_5:
	add	x0, sp, #8                      // =8
	mov	w2, #1
	str	w20, [sp, #12]
	bl	FlashEraseBlocks
.LBB67_6:
	adrp	x9, __stack_chk_guard
	ldur	x8, [x29, #-8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB67_8
// %bb.7:
	mov	w0, w19
	ldp	x20, x19, [sp, #128]            // 16-byte Folded Reload
	ldp	x29, x30, [sp, #112]            // 16-byte Folded Reload
	add	sp, sp, #144                    // =144
	hint	#29
	ret
.LBB67_8:
	bl	__stack_chk_fail
.Lfunc_end67:
	.size	FlashTestBlk, .Lfunc_end67-FlashTestBlk
                                        // -- End function
	.globl	P2V_plane                       // -- Begin function P2V_plane
	.p2align	2
	.type	P2V_plane,@function
P2V_plane:                              // @P2V_plane
// %bb.0:
	hint	#25
	adrp	x9, c_ftl_nand_planes_per_die
	adrp	x8, c_ftl_nand_blks_per_die
	ldrh	w9, [x9, :lo12:c_ftl_nand_planes_per_die]
	ldrh	w8, [x8, :lo12:c_ftl_nand_blks_per_die]
	and	w10, w0, #0xffff
	hint	#29
	udiv	w11, w10, w9
	udiv	w8, w10, w8
	msub	w10, w11, w9, w10
	madd	w0, w9, w8, w10
	ret
.Lfunc_end68:
	.size	P2V_plane, .Lfunc_end68-P2V_plane
                                        // -- End function
	.globl	ftl_cmp_data_ver                // -- Begin function ftl_cmp_data_ver
	.p2align	2
	.type	ftl_cmp_data_ver,@function
ftl_cmp_data_ver:                       // @ftl_cmp_data_ver
// %bb.0:
	hint	#25
	subs	w8, w0, w1
	mov	w9, #-2147483647
	sub	w10, w1, w0
	mov	w11, #-2147483648
	cmp	w8, w9
	cset	w8, lo
	cmp	w10, w11
	cset	w9, hi
	cmp	w0, w1
	csel	w0, w8, w9, hi
	hint	#29
	ret
.Lfunc_end69:
	.size	ftl_cmp_data_ver, .Lfunc_end69-ftl_cmp_data_ver
                                        // -- End function
	.globl	FtlFreeSysBlkQueueEmpty         // -- Begin function FtlFreeSysBlkQueueEmpty
	.p2align	2
	.type	FtlFreeSysBlkQueueEmpty,@function
FtlFreeSysBlkQueueEmpty:                // @FtlFreeSysBlkQueueEmpty
// %bb.0:
	hint	#25
	adrp	x8, gSysFreeQueue+6
	ldrh	w8, [x8, :lo12:gSysFreeQueue+6]
	hint	#29
	cmp	w8, #0                          // =0
	cset	w0, eq
	ret
.Lfunc_end70:
	.size	FtlFreeSysBlkQueueEmpty, .Lfunc_end70-FtlFreeSysBlkQueueEmpty
                                        // -- End function
	.globl	FtlFreeSysBlkQueueFull          // -- Begin function FtlFreeSysBlkQueueFull
	.p2align	2
	.type	FtlFreeSysBlkQueueFull,@function
FtlFreeSysBlkQueueFull:                 // @FtlFreeSysBlkQueueFull
// %bb.0:
	hint	#25
	adrp	x8, gSysFreeQueue+6
	ldrh	w8, [x8, :lo12:gSysFreeQueue+6]
	hint	#29
	cmp	w8, #1024                       // =1024
	cset	w0, eq
	ret
.Lfunc_end71:
	.size	FtlFreeSysBlkQueueFull, .Lfunc_end71-FtlFreeSysBlkQueueFull
                                        // -- End function
	.globl	FtlFreeSysBLkSort               // -- Begin function FtlFreeSysBLkSort
	.p2align	2
	.type	FtlFreeSysBLkSort,@function
FtlFreeSysBLkSort:                      // @FtlFreeSysBLkSort
// %bb.0:
	hint	#25
	adrp	x8, gSysFreeQueue+6
	ldrh	w8, [x8, :lo12:gSysFreeQueue+6]
	cbz	w8, .LBB72_5
// %bb.1:
	adrp	x8, g_sys_save_data+28
	ldrh	w8, [x8, :lo12:g_sys_save_data+28]
	and	w8, w8, #0x1f
	cbz	w8, .LBB72_5
// %bb.2:
	adrp	x9, gSysFreeQueue+2
	add	x9, x9, :lo12:gSysFreeQueue+2
	ldrh	w13, [x9]
	ldrh	w11, [x9, #2]
	adrp	x12, gSysFreeQueue+8
	mov	w10, wzr
	add	x12, x12, :lo12:gSysFreeQueue+8
.LBB72_3:                               // =>This Inner Loop Header: Depth=1
	and	x14, x13, #0xffff
	ldrh	w14, [x12, x14, lsl #1]
	add	w13, w13, #1                    // =1
	and	x15, x11, #0xffff
	add	w11, w11, #1                    // =1
	add	w10, w10, #1                    // =1
	and	w13, w13, #0x3ff
	cmp	w8, w10, uxth
	and	w11, w11, #0x3ff
	strh	w14, [x12, x15, lsl #1]
	b.hi	.LBB72_3
// %bb.4:
	strh	w13, [x9]
	strh	w11, [x9, #2]
.LBB72_5:
	hint	#29
	ret
.Lfunc_end72:
	.size	FtlFreeSysBLkSort, .Lfunc_end72-FtlFreeSysBLkSort
                                        // -- End function
	.globl	IsInFreeQueue                   // -- Begin function IsInFreeQueue
	.p2align	2
	.type	IsInFreeQueue,@function
IsInFreeQueue:                          // @IsInFreeQueue
// %bb.0:
	hint	#25
	adrp	x8, gSysFreeQueue+6
	ldrh	w8, [x8, :lo12:gSysFreeQueue+6]
	cmp	w8, #1024                       // =1024
	b.eq	.LBB73_5
// %bb.1:
	cbz	w8, .LBB73_5
// %bb.2:
	adrp	x9, gSysFreeQueue+2
	ldrh	w9, [x9, :lo12:gSysFreeQueue+2]
	adrp	x10, gSysFreeQueue
	add	x10, x10, :lo12:gSysFreeQueue
.LBB73_3:                               // =>This Inner Loop Header: Depth=1
	and	w11, w9, #0x3ff
	add	x11, x10, w11, uxtw #1
	ldrh	w11, [x11, #8]
	cmp	w11, w0, uxth
	b.eq	.LBB73_6
// %bb.4:                               //   in Loop: Header=BB73_3 Depth=1
	subs	w8, w8, #1                      // =1
	add	w9, w9, #1                      // =1
	b.ne	.LBB73_3
.LBB73_5:
	mov	w0, wzr
	hint	#29
	ret
.LBB73_6:
	mov	w0, #1
	hint	#29
	ret
.Lfunc_end73:
	.size	IsInFreeQueue, .Lfunc_end73-IsInFreeQueue
                                        // -- End function
	.globl	INSERT_FREE_LIST                // -- Begin function INSERT_FREE_LIST
	.p2align	2
	.type	INSERT_FREE_LIST,@function
INSERT_FREE_LIST:                       // @INSERT_FREE_LIST
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
                                        // kill: def $w0 killed $w0 def $x0
	mvn	w8, w0
	tst	w8, #0xffff
	mov	x29, sp
	b.eq	.LBB74_12
// %bb.1:
	adrp	x8, p_data_block_list_table
	ldr	x9, [x8, :lo12:p_data_block_list_table]
	and	x13, x0, #0xffff
	mov	w10, #6
	mov	w11, #-1
	madd	x9, x13, x10, x9
	mov	x10, x9
	str	w11, [x10], #2
	adrp	x12, p_free_data_block_list_head
	ldr	x11, [x12, :lo12:p_free_data_block_list_head]
	cbz	x11, .LBB74_11
// %bb.2:
	adrp	x15, p_erase_count_table
	ldr	x14, [x8, :lo12:p_data_block_list_table]
	ldr	x15, [x15, :lo12:p_erase_count_table]
	sub	x17, x11, x14
	ldrh	w16, [x15, x13, lsl #1]
	asr	x13, x17, #1
	mov	x17, #-6148914691236517206
	movk	x17, #43691
	mul	x13, x13, x17
	and	x17, x13, #0xffff
	ldrh	w17, [x15, x17, lsl #1]
                                        // kill: def $w13 killed $w13 killed $x13 def $x13
	cmp	w17, w16
	b.hs	.LBB74_8
// %bb.3:
	ldrh	w1, [x11]
	mov	w17, #65535
	cmp	w1, w17
	b.eq	.LBB74_7
// %bb.4:
	mov	w18, #6
.LBB74_5:                               // =>This Inner Loop Header: Depth=1
	mov	w13, w1
	and	x11, x13, #0xffff
	ldrh	w1, [x15, x11, lsl #1]
	madd	x11, x11, x18, x14
	cmp	w1, w16
	b.hs	.LBB74_8
// %bb.6:                               //   in Loop: Header=BB74_5 Depth=1
	ldrh	w1, [x11]
	cmp	w1, w17
	b.ne	.LBB74_5
.LBB74_7:
	strh	w13, [x10]
	strh	w0, [x11]
	b	.LBB74_12
.LBB74_8:
	ldrh	w14, [x11, #2]
	strh	w14, [x10]
	strh	w13, [x9]
	ldr	x10, [x12, :lo12:p_free_data_block_list_head]
	cmp	x11, x10
	b.eq	.LBB74_10
// %bb.9:
	ldrh	w9, [x11, #2]
	ldr	x8, [x8, :lo12:p_data_block_list_table]
	mov	w10, #6
	mul	x9, x9, x10
	strh	w0, [x8, x9]
	strh	w0, [x11, #2]
	b	.LBB74_12
.LBB74_10:
	strh	w0, [x11, #2]
.LBB74_11:
	str	x9, [x12, :lo12:p_free_data_block_list_head]
.LBB74_12:
	adrp	x8, g_num_free_superblocks
	ldrh	w9, [x8, :lo12:g_num_free_superblocks]
	adrp	x10, c_ftl_nand_data_blks_per_plane
	ldrh	w10, [x10, :lo12:c_ftl_nand_data_blks_per_plane]
	add	w9, w9, #1                      // =1
	strh	w9, [x8, :lo12:g_num_free_superblocks]
	cmp	w10, w9, uxth
	b.hs	.LBB74_14
// %bb.13:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.INSERT_FREE_LIST
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.INSERT_FREE_LIST
	mov	w2, #207
	bl	sftl_printk
.LBB74_14:
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end74:
	.size	INSERT_FREE_LIST, .Lfunc_end74-INSERT_FREE_LIST
                                        // -- End function
	.globl	insert_free_list                // -- Begin function insert_free_list
	.p2align	2
	.type	insert_free_list,@function
insert_free_list:                       // @insert_free_list
// %bb.0:
	hint	#25
                                        // kill: def $w0 killed $w0 def $x0
	mvn	w8, w0
	tst	w8, #0xffff
	b.eq	.LBB75_13
// %bb.1:
	adrp	x8, p_data_block_list_table
	ldr	x9, [x8, :lo12:p_data_block_list_table]
	and	x15, x0, #0xffff
	mov	w10, #6
	madd	x9, x15, x10, x9
	mov	w10, #-1
	mov	x11, x9
	str	w10, [x11], #2
	adrp	x10, p_free_data_block_list_head
	ldr	x12, [x10, :lo12:p_free_data_block_list_head]
	cbz	x12, .LBB75_12
// %bb.2:
	ldr	x13, [x8, :lo12:p_data_block_list_table]
	adrp	x14, p_erase_count_table
	ldr	x14, [x14, :lo12:p_erase_count_table]
	mov	x17, #-6148914691236517206
	sub	x16, x12, x13
	asr	x16, x16, #1
	movk	x17, #43691
	mul	x17, x16, x17
	and	x16, x17, #0xffff
	ldrh	w15, [x14, x15, lsl #1]
	ldrh	w16, [x14, x16, lsl #1]
	cmp	w16, w15
	b.hs	.LBB75_9
// %bb.3:
	ldrh	w16, [x12]
	mov	w18, #65535
	cmp	w16, w18
	b.eq	.LBB75_7
// %bb.4:
	mov	w1, #6
.LBB75_5:                               // =>This Inner Loop Header: Depth=1
	and	x12, x16, #0xffff
	ldrh	w17, [x14, x12, lsl #1]
	madd	x12, x12, x1, x13
	cmp	w17, w15
	b.hs	.LBB75_8
// %bb.6:                               //   in Loop: Header=BB75_5 Depth=1
	ldrh	w2, [x12]
	mov	w17, w16
	cmp	w2, w18
	mov	w16, w2
	b.ne	.LBB75_5
.LBB75_7:
	strh	w17, [x11]
	strh	w0, [x12]
	b	.LBB75_13
.LBB75_8:
	mov	w17, w16
.LBB75_9:
	ldrh	w13, [x12, #2]
	strh	w13, [x11]
	strh	w17, [x9]
	ldr	x11, [x10, :lo12:p_free_data_block_list_head]
	cmp	x12, x11
	b.eq	.LBB75_11
// %bb.10:
	ldrh	w9, [x12, #2]
	ldr	x8, [x8, :lo12:p_data_block_list_table]
	mov	w10, #6
	mul	x9, x9, x10
	strh	w0, [x8, x9]
	strh	w0, [x12, #2]
	b	.LBB75_13
.LBB75_11:
	strh	w0, [x12, #2]
.LBB75_12:
	str	x9, [x10, :lo12:p_free_data_block_list_head]
.LBB75_13:
	mov	w0, wzr
	hint	#29
	ret
.Lfunc_end75:
	.size	insert_free_list, .Lfunc_end75-insert_free_list
                                        // -- End function
	.globl	INSERT_DATA_LIST                // -- Begin function INSERT_DATA_LIST
	.p2align	2
	.type	INSERT_DATA_LIST,@function
INSERT_DATA_LIST:                       // @INSERT_DATA_LIST
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	mov	x29, sp
	bl	insert_data_list
	adrp	x8, g_num_data_superblocks
	ldrh	w9, [x8, :lo12:g_num_data_superblocks]
	adrp	x10, c_ftl_nand_data_blks_per_plane
	ldrh	w10, [x10, :lo12:c_ftl_nand_data_blks_per_plane]
	add	w9, w9, #1                      // =1
	strh	w9, [x8, :lo12:g_num_data_superblocks]
	cmp	w10, w9, uxth
	b.hs	.LBB76_2
// %bb.1:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.INSERT_DATA_LIST
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.INSERT_DATA_LIST
	mov	w2, #214
	bl	sftl_printk
.LBB76_2:
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end76:
	.size	INSERT_DATA_LIST, .Lfunc_end76-INSERT_DATA_LIST
                                        // -- End function
	.globl	insert_data_list                // -- Begin function insert_data_list
	.p2align	2
	.type	insert_data_list,@function
insert_data_list:                       // @insert_data_list
// %bb.0:
	hint	#25
	adrp	x16, c_ftl_nand_data_blks_per_plane
	ldrh	w8, [x16, :lo12:c_ftl_nand_data_blks_per_plane]
                                        // kill: def $w0 killed $w0 def $x0
	cmp	w8, w0, uxth
	b.ls	.LBB77_14
// %bb.1:
	adrp	x8, p_data_block_list_table
	ldr	x15, [x8, :lo12:p_data_block_list_table]
	and	x17, x0, #0xffff
	mov	w9, #6
	mov	w10, #-1
	madd	x9, x17, x9, x15
	mov	x12, x9
	str	w10, [x12], #2
	adrp	x10, p_data_block_list_head
	ldr	x11, [x10, :lo12:p_data_block_list_head]
	cbz	x11, .LBB77_13
// %bb.2:
	adrp	x14, p_valid_page_count_table
	ldr	x14, [x14, :lo12:p_valid_page_count_table]
	ldr	x13, [x8, :lo12:p_data_block_list_table]
	mov	w18, #6
	madd	x15, x17, x18, x15
	ldrh	w15, [x15, #4]
	ldrh	w2, [x14, x17, lsl #1]
	mov	w1, #43691
	sub	x18, x11, x13
	movk	w1, #43690, lsl #16
	lsr	x17, x18, #1
	mul	w17, w17, w1
	cmp	w15, #0                         // =0
	mul	w15, w15, w2
	and	w18, w17, #0xffff
	csinv	w15, w15, wzr, ne
	cmp	w18, w0, uxth
	b.eq	.LBB77_14
// %bb.3:
	ldrh	w16, [x16, :lo12:c_ftl_nand_data_blks_per_plane]
	cbz	w16, .LBB77_14
// %bb.4:
	mov	w18, #1
	mov	w1, #65535
	mov	w2, #6
.LBB77_5:                               // =>This Inner Loop Header: Depth=1
	and	x3, x17, #0xffff
	ldrh	w4, [x11, #4]
	ldrh	w3, [x14, x3, lsl #1]
	cmp	w4, #0                          // =0
	mul	w3, w4, w3
	csinv	w3, w3, wzr, ne
	cmp	w3, w15
	b.hs	.LBB77_9
// %bb.6:                               //   in Loop: Header=BB77_5 Depth=1
	ldrh	w3, [x11]
	cmp	x3, x1
	b.eq	.LBB77_11
// %bb.7:                               //   in Loop: Header=BB77_5 Depth=1
	cmp	w3, w0, uxth
	b.eq	.LBB77_14
// %bb.8:                               //   in Loop: Header=BB77_5 Depth=1
	add	w18, w18, #1                    // =1
	cmp	w16, w18, uxth
	madd	x11, x3, x2, x13
	mov	w17, w3
	b.hs	.LBB77_5
	b	.LBB77_14
.LBB77_9:
	strh	w17, [x9]
	ldrh	w13, [x11, #2]
	strh	w13, [x12]
	ldr	x12, [x10, :lo12:p_data_block_list_head]
	cmp	x11, x12
	b.eq	.LBB77_12
// %bb.10:
	ldrh	w9, [x11, #2]
	ldr	x8, [x8, :lo12:p_data_block_list_table]
	mov	w10, #6
	mul	x9, x9, x10
	strh	w0, [x8, x9]
	strh	w0, [x11, #2]
	b	.LBB77_14
.LBB77_11:
	adrp	x8, p_data_block_list_tail
	strh	w17, [x12]
	strh	w0, [x11]
	str	x9, [x8, :lo12:p_data_block_list_tail]
	b	.LBB77_14
.LBB77_12:
	strh	w0, [x11, #2]
.LBB77_13:
	str	x9, [x10, :lo12:p_data_block_list_head]
.LBB77_14:
	mov	w0, wzr
	hint	#29
	ret
.Lfunc_end77:
	.size	insert_data_list, .Lfunc_end77-insert_data_list
                                        // -- End function
	.globl	List_remove_node                // -- Begin function List_remove_node
	.p2align	2
	.type	List_remove_node,@function
List_remove_node:                       // @List_remove_node
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-48]!           // 16-byte Folded Spill
	stp	x22, x21, [sp, #16]             // 16-byte Folded Spill
	adrp	x22, p_data_block_list_table
	ldr	x8, [x22, :lo12:p_data_block_list_table]
                                        // kill: def $w1 killed $w1 def $x1
	and	x9, x1, #0xffff
	mov	w10, #6
	stp	x20, x19, [sp, #32]             // 16-byte Folded Spill
	madd	x20, x9, x10, x8
	mov	x21, x20
	ldrsh	w8, [x21, #2]!
	mov	x19, x0
	mov	x29, sp
	cmn	w8, #1                          // =1
	b.ne	.LBB78_3
// %bb.1:
	ldr	x8, [x19]
	cmp	x20, x8
	b.eq	.LBB78_3
// %bb.2:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.List_remove_node
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.List_remove_node
	mov	w2, #372
	bl	sftl_printk
.LBB78_3:
	ldr	x9, [x19]
	ldrh	w8, [x20]
	cmp	x20, x9
	b.eq	.LBB78_6
// %bb.4:
	mov	w9, #65535
	cmp	w8, w9
	b.eq	.LBB78_8
// %bb.5:
	ldr	x9, [x22, :lo12:p_data_block_list_table]
	ldrh	w10, [x21]
	mov	w11, #6
	madd	x8, x8, x11, x9
	strh	w10, [x8, #2]
	ldrh	w9, [x21]
	ldr	x8, [x22, :lo12:p_data_block_list_table]
	ldrh	w10, [x20]
	mul	x9, x9, x11
	strh	w10, [x8, x9]
	b	.LBB78_11
.LBB78_6:
	mov	w9, #65535
	cmp	w8, w9
	b.eq	.LBB78_10
// %bb.7:
	ldr	x10, [x22, :lo12:p_data_block_list_table]
	mov	w11, #6
	madd	x8, x8, x11, x10
	str	x8, [x19]
	strh	w9, [x8, #2]
	b	.LBB78_11
.LBB78_8:
	ldrh	w8, [x21]
	mov	w9, #65535
	cmp	x8, x9
	b.eq	.LBB78_11
// %bb.9:
	ldr	x10, [x22, :lo12:p_data_block_list_table]
	mov	w11, #6
	mul	x8, x8, x11
	strh	w9, [x10, x8]
	b	.LBB78_11
.LBB78_10:
	str	xzr, [x19]
.LBB78_11:
	mov	w8, #65535
	strh	w8, [x20]
	strh	w8, [x21]
	ldp	x20, x19, [sp, #32]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #48             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end78:
	.size	List_remove_node, .Lfunc_end78-List_remove_node
                                        // -- End function
	.globl	List_pop_index_node             // -- Begin function List_pop_index_node
	.p2align	2
	.type	List_pop_index_node,@function
List_pop_index_node:                    // @List_pop_index_node
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-64]!           // 16-byte Folded Spill
	ldr	x8, [x0]
	str	x23, [sp, #16]                  // 8-byte Folded Spill
	stp	x22, x21, [sp, #32]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #48]             // 16-byte Folded Spill
	mov	x29, sp
	cbz	x8, .LBB79_11
// %bb.1:
	adrp	x20, p_data_block_list_table
	ldr	x9, [x20, :lo12:p_data_block_list_table]
	mov	x19, x0
	tst	w1, #0xffff
	mov	x12, x8
	b.eq	.LBB79_5
// %bb.2:
	mov	w10, #65535
	mov	w11, #6
	mov	x12, x8
.LBB79_3:                               // =>This Inner Loop Header: Depth=1
	ldrh	w13, [x12]
	cmp	x13, x10
	b.eq	.LBB79_5
// %bb.4:                               //   in Loop: Header=BB79_3 Depth=1
	sub	w1, w1, #1                      // =1
	tst	w1, #0xffff
	madd	x12, x13, x11, x9
	b.ne	.LBB79_3
.LBB79_5:
	sub	x10, x12, x9
	mov	x11, #-6148914691236517206
	movk	x11, #43691
	asr	x10, x10, #1
	mul	x21, x10, x11
	and	x10, x21, #0xffff
	mov	w11, #6
	madd	x22, x10, x11, x9
	mov	x23, x22
	ldrsh	w9, [x23, #2]!
	cmn	w9, #1                          // =1
	b.ne	.LBB79_8
// %bb.6:
	cmp	x22, x8
	b.eq	.LBB79_8
// %bb.7:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.List_remove_node
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.List_remove_node
	mov	w2, #372
	bl	sftl_printk
	ldr	x8, [x19]
.LBB79_8:
	ldrh	w9, [x22]
	cmp	x22, x8
	b.eq	.LBB79_12
// %bb.9:
	mov	w8, #65535
	cmp	w9, w8
	b.eq	.LBB79_14
// %bb.10:
	ldr	x8, [x20, :lo12:p_data_block_list_table]
	ldrh	w10, [x23]
	mov	w11, #6
	madd	x8, x9, x11, x8
	strh	w10, [x8, #2]
	ldrh	w9, [x23]
	ldr	x8, [x20, :lo12:p_data_block_list_table]
	ldrh	w10, [x22]
	mul	x9, x9, x11
	strh	w10, [x8, x9]
	b	.LBB79_17
.LBB79_11:
	mov	w0, #65535
	b	.LBB79_18
.LBB79_12:
	mov	w8, #65535
	cmp	w9, w8
	b.eq	.LBB79_16
// %bb.13:
	ldr	x10, [x20, :lo12:p_data_block_list_table]
	mov	w11, #6
	madd	x9, x9, x11, x10
	str	x9, [x19]
	strh	w8, [x9, #2]
	b	.LBB79_17
.LBB79_14:
	ldrh	w8, [x23]
	mov	w9, #65535
	cmp	x8, x9
	b.eq	.LBB79_17
// %bb.15:
	ldr	x10, [x20, :lo12:p_data_block_list_table]
	mov	w11, #6
	mul	x8, x8, x11
	strh	w9, [x10, x8]
	b	.LBB79_17
.LBB79_16:
	str	xzr, [x19]
.LBB79_17:
	mov	w8, #65535
	and	w0, w21, #0xffff
	strh	w8, [x22]
	strh	w8, [x23]
.LBB79_18:
	ldp	x20, x19, [sp, #48]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #32]             // 16-byte Folded Reload
	ldr	x23, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #64             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end79:
	.size	List_pop_index_node, .Lfunc_end79-List_pop_index_node
                                        // -- End function
	.globl	List_pop_head_node              // -- Begin function List_pop_head_node
	.p2align	2
	.type	List_pop_head_node,@function
List_pop_head_node:                     // @List_pop_head_node
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	mov	w1, wzr
	mov	x29, sp
	bl	List_pop_index_node
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end80:
	.size	List_pop_head_node, .Lfunc_end80-List_pop_head_node
                                        // -- End function
	.globl	List_update_data_list           // -- Begin function List_update_data_list
	.p2align	2
	.type	List_update_data_list,@function
List_update_data_list:                  // @List_update_data_list
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-96]!           // 16-byte Folded Spill
	adrp	x8, g_active_superblock
	ldrh	w8, [x8, :lo12:g_active_superblock]
	stp	x28, x27, [sp, #16]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #32]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #48]             // 16-byte Folded Spill
	cmp	w8, w0, uxth
	stp	x22, x21, [sp, #64]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #80]             // 16-byte Folded Spill
	mov	x29, sp
	b.eq	.LBB81_25
// %bb.1:
	adrp	x8, g_buffer_superblock
	ldrh	w8, [x8, :lo12:g_buffer_superblock]
	mov	w19, w0
	cmp	w8, w0, uxth
	b.eq	.LBB81_25
// %bb.2:
	adrp	x8, g_gc_temp_superblock
	ldrh	w8, [x8, :lo12:g_gc_temp_superblock]
	cmp	w8, w19, uxth
	b.eq	.LBB81_25
// %bb.3:
	adrp	x20, p_data_block_list_table
	ldr	x8, [x20, :lo12:p_data_block_list_table]
	adrp	x21, p_data_block_list_head
	ldr	x9, [x21, :lo12:p_data_block_list_head]
	and	x22, x19, #0xffff
	mov	w10, #6
	madd	x25, x22, x10, x8
	cmp	x25, x9
	b.eq	.LBB81_25
// %bb.4:
	adrp	x23, p_valid_page_count_table
	ldr	x9, [x23, :lo12:p_valid_page_count_table]
	mov	w24, #6
	madd	x28, x22, x24, x8
	mov	w10, #65535
	ldrh	w8, [x9, x22, lsl #1]
	ldrh	w27, [x28, #2]!
	ldrh	w9, [x28, #2]
	mul	w8, w9, w8
	cmp	w9, #0                          // =0
	csinv	w26, w8, wzr, ne
	cmp	w27, w10
	b.ne	.LBB81_9
// %bb.5:
	ldrsh	w8, [x25]
	mov	w27, #65535
	cmn	w8, #1                          // =1
	b.ne	.LBB81_9
// %bb.6:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.List_update_data_list
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.List_update_data_list
	mov	w2, #463
	bl	sftl_printk
	ldrh	w8, [x28]
	cmp	w8, w27
	mov	w27, w8
	b.ne	.LBB81_9
// %bb.7:
	ldrsh	w8, [x25]
	cmn	w8, #1                          // =1
	b.eq	.LBB81_25
// %bb.8:
	mov	w27, #65535
.LBB81_9:
	ldr	x8, [x20, :lo12:p_data_block_list_table]
	ldr	x9, [x23, :lo12:p_valid_page_count_table]
	umaddl	x10, w27, w24, x8
	ldrh	w9, [x9, w27, uxtw #1]
	ldrh	w10, [x10, #4]
	mul	w9, w10, w9
	cmp	w10, #0                         // =0
	csinv	w9, w9, wzr, ne
	cmp	w26, w9
	b.hs	.LBB81_25
// %bb.10:
	mov	w9, #6
	madd	x22, x22, x9, x8
	mov	x23, x22
	ldrsh	w8, [x23, #2]!
	ldr	x9, [x21, :lo12:p_data_block_list_head]
	cmn	w8, #1                          // =1
	b.ne	.LBB81_13
// %bb.11:
	cmp	x22, x9
	b.eq	.LBB81_13
// %bb.12:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.List_remove_node
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.List_remove_node
	mov	w2, #372
	bl	sftl_printk
	ldr	x9, [x21, :lo12:p_data_block_list_head]
.LBB81_13:
	ldrh	w8, [x22]
	cmp	x22, x9
	b.eq	.LBB81_16
// %bb.14:
	mov	w9, #65535
	cmp	w8, w9
	b.eq	.LBB81_18
// %bb.15:
	ldr	x9, [x20, :lo12:p_data_block_list_table]
	ldrh	w10, [x23]
	mov	w11, #6
	madd	x8, x8, x11, x9
	strh	w10, [x8, #2]
	ldrh	w9, [x23]
	ldr	x8, [x20, :lo12:p_data_block_list_table]
	ldrh	w10, [x22]
	mul	x9, x9, x11
	strh	w10, [x8, x9]
	b	.LBB81_21
.LBB81_16:
	mov	w9, #65535
	cmp	w8, w9
	b.eq	.LBB81_20
// %bb.17:
	ldr	x10, [x20, :lo12:p_data_block_list_table]
	mov	w11, #6
	madd	x8, x8, x11, x10
	str	x8, [x21, :lo12:p_data_block_list_head]
	strh	w9, [x8, #2]
	b	.LBB81_21
.LBB81_18:
	ldrh	w8, [x23]
	mov	w9, #65535
	cmp	x8, x9
	b.eq	.LBB81_21
// %bb.19:
	ldr	x10, [x20, :lo12:p_data_block_list_table]
	mov	w11, #6
	mul	x8, x8, x11
	strh	w9, [x10, x8]
	b	.LBB81_21
.LBB81_20:
	str	xzr, [x21, :lo12:p_data_block_list_head]
.LBB81_21:
	mov	w8, #65535
	strh	w8, [x22]
	strh	w8, [x23]
	adrp	x20, g_num_data_superblocks
	ldrh	w8, [x20, :lo12:g_num_data_superblocks]
	cbnz	w8, .LBB81_23
// %bb.22:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.List_update_data_list
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.List_update_data_list
	mov	w2, #474
	bl	sftl_printk
	ldrh	w8, [x20, :lo12:g_num_data_superblocks]
.LBB81_23:
	sub	w8, w8, #1                      // =1
	mov	w0, w19
	strh	w8, [x20, :lo12:g_num_data_superblocks]
	bl	insert_data_list
	ldrh	w8, [x20, :lo12:g_num_data_superblocks]
	adrp	x9, c_ftl_nand_data_blks_per_plane
	ldrh	w9, [x9, :lo12:c_ftl_nand_data_blks_per_plane]
	add	w8, w8, #1                      // =1
	strh	w8, [x20, :lo12:g_num_data_superblocks]
	cmp	w9, w8, uxth
	b.hs	.LBB81_25
// %bb.24:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.INSERT_DATA_LIST
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.INSERT_DATA_LIST
	mov	w2, #214
	bl	sftl_printk
.LBB81_25:
	ldp	x20, x19, [sp, #80]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #64]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #48]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #32]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #96             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end81:
	.size	List_update_data_list, .Lfunc_end81-List_update_data_list
                                        // -- End function
	.globl	load_l2p_region                 // -- Begin function load_l2p_region
	.p2align	2
	.type	load_l2p_region,@function
load_l2p_region:                        // @load_l2p_region
// %bb.0:
	hint	#25
	sub	sp, sp, #112                    // =112
	stp	x24, x23, [sp, #64]             // 16-byte Folded Spill
	adrp	x24, c_ftl_nand_map_region_num
	ldrh	w8, [x24, :lo12:c_ftl_nand_map_region_num]
	stp	x20, x19, [sp, #96]             // 16-byte Folded Spill
	and	w20, w0, #0xffff
	stp	x22, x21, [sp, #80]             // 16-byte Folded Spill
	mov	w19, w0
	cmp	w8, w20
	mov	w22, w1
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	stp	x28, x27, [sp, #32]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #48]             // 16-byte Folded Spill
	add	x29, sp, #16                    // =16
	b.hs	.LBB82_2
// %bb.1:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.load_l2p_region
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.load_l2p_region
	mov	w2, #485
	bl	sftl_printk
.LBB82_2:
	adrp	x25, p_map_region_ppn_table
	ldr	x8, [x25, :lo12:p_map_region_ppn_table]
	and	x9, x19, #0xffff
	ldr	w21, [x8, x9, lsl #2]
	cbz	w21, .LBB82_6
// %bb.3:
	adrp	x26, p_l2p_ram_map
	ldr	x8, [x26, :lo12:p_l2p_ram_map]
	adrp	x23, req_sys
	add	x23, x23, :lo12:req_sys
	adrp	x9, p_sys_spare_buf
	str	w21, [x23, #4]
	add	x8, x8, w22, uxth #4
	ldr	x8, [x8, #8]
	ldr	x9, [x9, :lo12:p_sys_spare_buf]
	mov	w1, #1
	mov	x0, x23
	and	x22, x22, #0xffff
	stp	x8, x9, [x23, #8]
	bl	FlashReadPages
	ldr	x28, [x23, #16]
	ldrh	w8, [x28, #8]
	cmp	w8, w19, uxth
	b.ne	.LBB82_7
// %bb.4:
	adrp	x8, req_sys
	ldr	w8, [x8, :lo12:req_sys]
	cmp	w8, #256                        // =256
	b.ne	.LBB82_15
// %bb.5:
	adrp	x0, .L.str.90
	add	x0, x0, :lo12:.L.str.90
	mov	w1, w20
	mov	w2, w21
	bl	sftl_printk
	ldr	x8, [x26, :lo12:p_l2p_ram_map]
	adrp	x0, gL2pMapInfo
	add	x0, x0, :lo12:gL2pMapInfo
	mov	w1, w20
	add	x8, x8, x22, lsl #4
	ldr	x2, [x8, #8]
	bl	FtlMapWritePage
	b	.LBB82_15
.LBB82_6:
	adrp	x20, p_l2p_ram_map
	ldr	x8, [x20, :lo12:p_l2p_ram_map]
                                        // kill: def $w22 killed $w22 killed $x22 def $x22
	and	x9, x22, #0xffff
	lsl	x21, x9, #4
	adrp	x10, c_ftl_nand_byte_pre_page
	add	x8, x8, x21
	ldr	x0, [x8, #8]
	ldrh	w2, [x10, :lo12:c_ftl_nand_byte_pre_page]
	mov	w1, #255
	bl	memset
	ldr	x8, [x20, :lo12:p_l2p_ram_map]
	strh	w19, [x8, x21]
	ldr	x8, [x20, :lo12:p_l2p_ram_map]
	add	x8, x8, x21
	str	wzr, [x8, #4]
	b	.LBB82_18
.LBB82_7:
	adrp	x0, .L.str.87
	add	x0, x0, :lo12:.L.str.87
	mov	w1, w20
	mov	w2, w21
	str	x22, [sp, #8]                   // 8-byte Folded Spill
	bl	sftl_printk
	adrp	x8, req_sys+16
	ldr	x20, [x8, :lo12:req_sys+16]
	adrp	x0, .L.str.116
	adrp	x1, .L.str.88
	add	x0, x0, :lo12:.L.str.116
	add	x1, x1, :lo12:.L.str.88
	mov	x2, x20
	mov	w3, wzr
	bl	sftl_printk
	ldr	w1, [x20]
	adrp	x21, .L.str.117
	add	x21, x21, :lo12:.L.str.117
	mov	x0, x21
	bl	sftl_printk
	ldr	w1, [x20, #4]
	mov	x0, x21
	bl	sftl_printk
	ldr	w1, [x20, #8]
	mov	x0, x21
	bl	sftl_printk
	ldr	w1, [x20, #12]
	mov	x0, x21
	bl	sftl_printk
	adrp	x0, .L.str.120
	add	x0, x0, :lo12:.L.str.120
	bl	sftl_printk
	ldrh	w8, [x24, :lo12:c_ftl_nand_map_region_num]
	cbz	x8, .LBB82_14
// %bb.8:
	ldr	x21, [x25, :lo12:p_map_region_ppn_table]
	adrp	x22, .L.str.116
	adrp	x23, .L.str.89
	adrp	x24, .L.str.117
	adrp	x25, .L.str.120
	mov	x20, xzr
	mov	w27, wzr
	lsl	x26, x8, #2
	add	x22, x22, :lo12:.L.str.116
	add	x23, x23, :lo12:.L.str.89
	add	x24, x24, :lo12:.L.str.117
	add	x25, x25, :lo12:.L.str.120
	b	.LBB82_11
.LBB82_9:                               //   in Loop: Header=BB82_11 Depth=1
	ldr	w1, [x21, x20]
	mov	x0, x24
	bl	sftl_printk
	add	w27, w27, #1                    // =1
	cmp	w27, #16                        // =16
	b.hs	.LBB82_13
.LBB82_10:                              //   in Loop: Header=BB82_11 Depth=1
	add	x20, x20, #4                    // =4
	cmp	x26, x20
	b.eq	.LBB82_14
.LBB82_11:                              // =>This Inner Loop Header: Depth=1
	cbnz	w27, .LBB82_9
// %bb.12:                              //   in Loop: Header=BB82_11 Depth=1
	mov	x0, x22
	mov	x1, x23
	mov	x2, x21
	mov	w3, w20
	bl	sftl_printk
	b	.LBB82_9
.LBB82_13:                              //   in Loop: Header=BB82_11 Depth=1
	mov	x0, x25
	bl	sftl_printk
	mov	w27, wzr
	b	.LBB82_10
.LBB82_14:
	adrp	x0, .L.str.120
	add	x0, x0, :lo12:.L.str.120
	bl	sftl_printk
	ldr	x22, [sp, #8]                   // 8-byte Folded Reload
	adrp	x26, p_l2p_ram_map
.LBB82_15:
	ldrh	w8, [x28, #8]
	cmp	w8, w19, uxth
	b.eq	.LBB82_17
// %bb.16:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.load_l2p_region
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.load_l2p_region
	mov	w2, #508
	bl	sftl_printk
.LBB82_17:
	ldr	x8, [x26, :lo12:p_l2p_ram_map]
	lsl	x9, x22, #4
	add	x8, x8, x9
	str	wzr, [x8, #4]
	ldr	x8, [x26, :lo12:p_l2p_ram_map]
	strh	w19, [x8, x9]
.LBB82_18:
	ldp	x20, x19, [sp, #96]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #80]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #64]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #48]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	add	sp, sp, #112                    // =112
	hint	#29
	ret
.Lfunc_end82:
	.size	load_l2p_region, .Lfunc_end82-load_l2p_region
                                        // -- End function
	.globl	rknand_print_hex                // -- Begin function rknand_print_hex
	.p2align	2
	.type	rknand_print_hex,@function
rknand_print_hex:                       // @rknand_print_hex
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-96]!           // 16-byte Folded Spill
	stp	x28, x27, [sp, #16]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #32]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #48]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #64]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #80]             // 16-byte Folded Spill
	mov	x29, sp
	cbz	w3, .LBB83_12
// %bb.1:
	adrp	x23, .L.str.116
	adrp	x26, .L.str.117
	adrp	x27, .L.str.119
	mov	w19, w2
	mov	x20, x1
	mov	x21, x0
	mov	w22, wzr
	mov	x28, xzr
	mov	w25, wzr
	mov	w24, w3
	add	x23, x23, :lo12:.L.str.116
	add	x26, x26, :lo12:.L.str.117
	add	x27, x27, :lo12:.L.str.119
	b	.LBB83_5
.LBB83_2:                               //   in Loop: Header=BB83_5 Depth=1
	ldrb	w1, [x20, x28]
	mov	x0, x27
.LBB83_3:                               //   in Loop: Header=BB83_5 Depth=1
	bl	sftl_printk
	add	w25, w25, #1                    // =1
	cmp	w25, #16                        // =16
	b.hs	.LBB83_11
.LBB83_4:                               //   in Loop: Header=BB83_5 Depth=1
	add	x28, x28, #1                    // =1
	cmp	x24, x28
	add	w22, w22, w19
	b.eq	.LBB83_12
.LBB83_5:                               // =>This Inner Loop Header: Depth=1
	cbz	w25, .LBB83_9
// %bb.6:                               //   in Loop: Header=BB83_5 Depth=1
	cmp	w19, #2                         // =2
	b.eq	.LBB83_10
.LBB83_7:                               //   in Loop: Header=BB83_5 Depth=1
	cmp	w19, #4                         // =4
	b.ne	.LBB83_2
// %bb.8:                               //   in Loop: Header=BB83_5 Depth=1
	ldr	w1, [x20, x28, lsl #2]
	mov	x0, x26
	b	.LBB83_3
.LBB83_9:                               //   in Loop: Header=BB83_5 Depth=1
	mov	x0, x23
	mov	x1, x21
	mov	x2, x20
	mov	w3, w22
	bl	sftl_printk
	cmp	w19, #2                         // =2
	b.ne	.LBB83_7
.LBB83_10:                              //   in Loop: Header=BB83_5 Depth=1
	ldrh	w1, [x20, x28, lsl #1]
	adrp	x0, .L.str.118
	add	x0, x0, :lo12:.L.str.118
	b	.LBB83_3
.LBB83_11:                              //   in Loop: Header=BB83_5 Depth=1
	adrp	x0, .L.str.120
	add	x0, x0, :lo12:.L.str.120
	bl	sftl_printk
	mov	w25, wzr
	b	.LBB83_4
.LBB83_12:
	adrp	x0, .L.str.120
	add	x0, x0, :lo12:.L.str.120
	bl	sftl_printk
	ldp	x20, x19, [sp, #80]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #64]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #48]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #32]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #16]             // 16-byte Folded Reload
	ldp	x29, x30, [sp], #96             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end83:
	.size	rknand_print_hex, .Lfunc_end83-rknand_print_hex
                                        // -- End function
	.globl	FtlMapWritePage                 // -- Begin function FtlMapWritePage
	.p2align	2
	.type	FtlMapWritePage,@function
FtlMapWritePage:                        // @FtlMapWritePage
// %bb.0:
	hint	#25
	sub	sp, sp, #112                    // =112
	stp	x26, x25, [sp, #48]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #64]             // 16-byte Folded Spill
	adrp	x26, req_sys+4
	adrp	x24, req_sys
	stp	x28, x27, [sp, #32]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #80]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #96]             // 16-byte Folded Spill
	mov	x19, x2
	mov	w20, w1
	mov	x21, x0
	mov	w9, wzr
	mov	w27, w1
	adrp	x28, g_totle_l2p_write_count
	adrp	x25, c_ftl_nand_page_pre_slc_blk
	add	x26, x26, :lo12:req_sys+4
	adrp	x22, p_sys_spare_buf
	adrp	x23, c_ftl_nand_byte_pre_page
	add	x24, x24, :lo12:req_sys
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	add	x29, sp, #16                    // =16
.LBB84_1:                               // =>This Loop Header: Depth=1
                                        //     Child Loop BB84_3 Depth 2
                                        //       Child Loop BB84_12 Depth 3
	stur	w9, [x29, #-4]                  // 4-byte Folded Spill
	b	.LBB84_3
.LBB84_2:                               //   in Loop: Header=BB84_3 Depth=2
	str	wzr, [x21, #56]
.LBB84_3:                               //   Parent Loop BB84_1 Depth=1
                                        // =>  This Loop Header: Depth=2
                                        //       Child Loop BB84_12 Depth 3
	ldr	w8, [x28, :lo12:g_totle_l2p_write_count]
	add	w8, w8, #1                      // =1
	str	w8, [x28, :lo12:g_totle_l2p_write_count]
	ldrh	w8, [x25, :lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w9, [x21, #2]
	sub	w8, w8, #1                      // =1
	cmp	w8, w9
	b.le	.LBB84_5
// %bb.4:                               //   in Loop: Header=BB84_3 Depth=2
	ldrh	w8, [x21]
	mov	w9, #65535
	cmp	w8, w9
	b.ne	.LBB84_6
.LBB84_5:                               //   in Loop: Header=BB84_3 Depth=2
	mov	x0, x21
	bl	Ftl_write_map_blk_to_last_page
	ldrh	w8, [x21]
.LBB84_6:                               //   in Loop: Header=BB84_3 Depth=2
	ldr	x9, [x21, #16]
	ldrh	w9, [x9, w8, uxtw #1]
	cbnz	w9, .LBB84_8
// %bb.7:                               //   in Loop: Header=BB84_3 Depth=2
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlMapWritePage
	mov	w2, #700
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlMapWritePage
	bl	sftl_printk
	ldrh	w8, [x21]
.LBB84_8:                               //   in Loop: Header=BB84_3 Depth=2
	ldrh	w9, [x21, #10]
	cmp	w8, w9
	b.lo	.LBB84_10
// %bb.9:                               //   in Loop: Header=BB84_3 Depth=2
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlMapWritePage
	mov	w2, #701
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlMapWritePage
	bl	sftl_printk
	ldrh	w8, [x21]
.LBB84_10:                              //   in Loop: Header=BB84_3 Depth=2
	ldr	x9, [x21, #16]
	ldr	x10, [x22, :lo12:p_sys_spare_buf]
	ldrh	w9, [x9, w8, uxtw #1]
	ldrh	w8, [x21, #2]
	stur	x19, [x26, #4]
	stur	x10, [x26, #12]
	orr	w8, w8, w9, lsl #10
	str	w8, [x26]
	stp	xzr, xzr, [x10]
	ldur	x8, [x26, #12]
	ldr	w10, [x21, #48]
	strh	w20, [x8, #8]
	str	w10, [x8, #4]
	ldrh	w10, [x21, #4]
	strh	w9, [x8, #2]
	strh	w10, [x8]
	ldrh	w9, [x23, :lo12:c_ftl_nand_byte_pre_page]
	mov	w10, #42982
	movk	w10, #18374, lsl #16
	cbz	x9, .LBB84_13
// %bb.11:                              //   in Loop: Header=BB84_3 Depth=2
	ldur	x11, [x26, #4]
	mov	w10, #42982
	movk	w10, #18374, lsl #16
.LBB84_12:                              //   Parent Loop BB84_1 Depth=1
                                        //     Parent Loop BB84_3 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	ldrb	w12, [x11], #1
	lsr	w13, w10, #2
	add	w13, w13, w10, lsl #5
	subs	x9, x9, #1                      // =1
	add	w12, w13, w12
	eor	w10, w12, w10
	b.ne	.LBB84_12
.LBB84_13:                              //   in Loop: Header=BB84_3 Depth=2
	mov	w1, #1
	mov	w2, #1
	mov	x0, x24
	str	w10, [x8, #12]
	bl	FlashProgPages
	ldrh	w8, [x21, #2]
	add	w8, w8, #1                      // =1
	strh	w8, [x21, #2]
	ldp	w8, w1, [x24]
	cmn	w8, #1                          // =1
	b.eq	.LBB84_17
// %bb.14:                              //   in Loop: Header=BB84_3 Depth=2
	ldr	x8, [x21, #40]
	str	w1, [x8, x27, lsl #2]
	ldrh	w8, [x21, #2]
	cmp	w8, #1                          // =1
	b.eq	.LBB84_2
// %bb.15:                              //   in Loop: Header=BB84_3 Depth=2
	adrp	x8, req_sys
	ldr	w8, [x8, :lo12:req_sys]
	cmp	w8, #256                        // =256
	b.eq	.LBB84_2
// %bb.16:                              //   in Loop: Header=BB84_3 Depth=2
	ldr	w9, [x21, #56]
	cbnz	w9, .LBB84_2
	b	.LBB84_22
.LBB84_17:                              //   in Loop: Header=BB84_1 Depth=1
	adrp	x0, .L.str.92
	add	x0, x0, :lo12:.L.str.92
	bl	sftl_printk
	ldrh	w8, [x21, #2]
	cmp	w8, #2                          // =2
	b.hi	.LBB84_19
// %bb.18:                              //   in Loop: Header=BB84_1 Depth=1
	ldrh	w8, [x25, :lo12:c_ftl_nand_page_pre_slc_blk]
	sub	w8, w8, #1                      // =1
	strh	w8, [x21, #2]
.LBB84_19:                              //   in Loop: Header=BB84_1 Depth=1
	ldur	w9, [x29, #-4]                  // 4-byte Folded Reload
	and	w8, w9, #0xffff
	cmp	w8, #3                          // =3
	b.hs	.LBB84_25
// %bb.20:                              //   in Loop: Header=BB84_1 Depth=1
	ldr	w8, [x21, #52]
	add	w9, w9, #1                      // =1
	cbz	w8, .LBB84_1
.LBB84_21:                              // =>This Inner Loop Header: Depth=1
	b	.LBB84_21
.LBB84_22:
	cmn	w8, #1                          // =1
	b.ne	.LBB84_24
// %bb.23:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlMapWritePage
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlMapWritePage
	mov	w2, #745
	bl	sftl_printk
.LBB84_24:
	ldp	x20, x19, [sp, #96]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #80]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #64]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #48]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	add	sp, sp, #112                    // =112
	hint	#29
	ret
.LBB84_25:
	adrp	x8, req_sys+4
	ldr	w1, [x8, :lo12:req_sys+4]
	adrp	x0, .L.str.93
	add	x0, x0, :lo12:.L.str.93
	mov	w2, #4
	bl	sftl_printk
.LBB84_26:                              // =>This Inner Loop Header: Depth=1
	b	.LBB84_26
.Lfunc_end84:
	.size	FtlMapWritePage, .Lfunc_end84-FtlMapWritePage
                                        // -- End function
	.globl	ftl_free_no_use_map_blk         // -- Begin function ftl_free_no_use_map_blk
	.p2align	2
	.type	ftl_free_no_use_map_blk,@function
ftl_free_no_use_map_blk:                // @ftl_free_no_use_map_blk
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-96]!           // 16-byte Folded Spill
	stp	x22, x21, [sp, #64]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #80]             // 16-byte Folded Spill
	ldrh	w8, [x0, #10]
	ldp	x20, x22, [x0, #32]
	ldr	x21, [x0, #16]
	mov	x19, x0
	lsl	x2, x8, #1
	mov	x0, x20
	mov	w1, wzr
	stp	x28, x27, [sp, #16]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #32]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #48]             // 16-byte Folded Spill
	mov	x29, sp
	bl	memset
	ldrh	w10, [x19, #6]
	cbz	w10, .LBB85_11
// %bb.1:
	ldrh	w9, [x19, #10]
	mov	x8, xzr
	mov	w11, w9
	b	.LBB85_4
.LBB85_2:                               //   in Loop: Header=BB85_4 Depth=1
	ldrh	w10, [x19, #6]
	mov	w11, w9
.LBB85_3:                               //   in Loop: Header=BB85_4 Depth=1
	add	x8, x8, #1                      // =1
	cmp	x8, w10, uxth
	b.hs	.LBB85_11
.LBB85_4:                               // =>This Loop Header: Depth=1
                                        //     Child Loop BB85_7 Depth 2
	tst	w11, #0xffff
	b.eq	.LBB85_10
// %bb.5:                               //   in Loop: Header=BB85_4 Depth=1
	ldr	w10, [x22, x8, lsl #2]
	mov	x11, xzr
	b	.LBB85_7
.LBB85_6:                               //   in Loop: Header=BB85_7 Depth=2
	add	x11, x11, #1                    // =1
	cmp	x11, w9, uxth
	b.hs	.LBB85_2
.LBB85_7:                               //   Parent Loop BB85_4 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldrh	w12, [x21, x11, lsl #1]
	lsr	w13, w10, #10
	cmp	w12, w13, uxth
	b.ne	.LBB85_6
// %bb.8:                               //   in Loop: Header=BB85_7 Depth=2
	cbz	w12, .LBB85_6
// %bb.9:                               //   in Loop: Header=BB85_7 Depth=2
	lsl	x9, x11, #1
	ldrh	w12, [x20, x9]
	add	w12, w12, #1                    // =1
	strh	w12, [x20, x9]
	ldrh	w9, [x19, #10]
	b	.LBB85_6
.LBB85_10:                              //   in Loop: Header=BB85_4 Depth=1
	mov	w11, wzr
	b	.LBB85_3
.LBB85_11:
	adrp	x8, c_ftl_nand_page_pre_slc_blk
	ldrh	w8, [x8, :lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w9, [x19]
	strh	w8, [x20, x9, lsl #1]
	ldrh	w8, [x19, #10]
	cbz	w8, .LBB85_23
// %bb.12:
	ldrh	w9, [x20], #2
	adrp	x26, gSysFreeQueue
	mov	x23, xzr
	mov	w22, wzr
	mov	w11, #65535
	adrp	x27, g_totle_sys_slc_erase_count
	add	x26, x26, :lo12:gSysFreeQueue
	mov	w24, w9
.LBB85_13:                              // =>This Inner Loop Header: Depth=1
	and	w10, w24, #0xffff
	cmp	w10, w9, uxth
	b.ls	.LBB85_15
// %bb.14:                              //   in Loop: Header=BB85_13 Depth=1
	ldrh	w10, [x21, x23, lsl #1]
	cmp	w10, #0                         // =0
	csel	w24, w24, w9, eq
	csel	w22, w22, w23, eq
.LBB85_15:                              //   in Loop: Header=BB85_13 Depth=1
	tst	w9, #0xffff
	b.ne	.LBB85_21
// %bb.16:                              //   in Loop: Header=BB85_13 Depth=1
	ldrh	w25, [x21, x23, lsl #1]
	cbz	w25, .LBB85_21
// %bb.17:                              //   in Loop: Header=BB85_13 Depth=1
	cmp	w25, w11
	b.eq	.LBB85_20
// %bb.18:                              //   in Loop: Header=BB85_13 Depth=1
	adrp	x8, gSysFreeQueue+6
	ldrh	w8, [x8, :lo12:gSysFreeQueue+6]
	cmp	w8, #1024                       // =1024
	b.eq	.LBB85_20
// %bb.19:                              //   in Loop: Header=BB85_13 Depth=1
	adrp	x12, req_erase
	ldr	x8, [x12, :lo12:req_erase]
	adrp	x9, c_ftl_nand_blks_per_die
	ldrh	w9, [x9, :lo12:c_ftl_nand_blks_per_die]
	adrp	x10, c_ftl_nand_planes_per_die
	lsl	w11, w25, #10
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_per_die]
	str	w11, [x8, #4]
	ldr	x0, [x12, :lo12:req_erase]
	udiv	w8, w25, w9
	msub	w8, w8, w9, w25
	mov	w2, #1
	udiv	w28, w8, w10
	bl	FlashEraseBlocks
	adrp	x8, p_erase_count_table
	ldr	x8, [x8, :lo12:p_erase_count_table]
	lsl	x9, x28, #1
	mov	w11, #65535
	ldrh	w10, [x8, x9]
	add	w10, w10, #1                    // =1
	strh	w10, [x8, x9]
	ldr	w8, [x27, :lo12:g_totle_sys_slc_erase_count]
	ldrh	w9, [x26, #6]
	ldrh	w10, [x26, #4]
	add	w8, w8, #1                      // =1
	add	w9, w9, #1                      // =1
	str	w8, [x27, :lo12:g_totle_sys_slc_erase_count]
	add	x8, x26, x10, lsl #1
	add	w10, w10, #1                    // =1
	strh	w9, [x26, #6]
	strh	w25, [x8, #8]
	and	w8, w10, #0x3ff
	strh	w8, [x26, #4]
.LBB85_20:                              //   in Loop: Header=BB85_13 Depth=1
	strh	wzr, [x21, x23, lsl #1]
	ldrh	w9, [x19, #8]
	ldrh	w8, [x19, #10]
	sub	w9, w9, #1                      // =1
	strh	w9, [x19, #8]
.LBB85_21:                              //   in Loop: Header=BB85_13 Depth=1
	add	x10, x23, #1                    // =1
	cmp	x10, w8, uxth
	b.hs	.LBB85_24
// %bb.22:                              //   in Loop: Header=BB85_13 Depth=1
	ldrh	w9, [x20, x23, lsl #1]
	mov	x23, x10
	b	.LBB85_13
.LBB85_23:
	mov	w22, wzr
.LBB85_24:
	and	w0, w22, #0xffff
	ldp	x20, x19, [sp, #80]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #64]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #48]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #32]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #16]             // 16-byte Folded Reload
	ldp	x29, x30, [sp], #96             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end85:
	.size	ftl_free_no_use_map_blk, .Lfunc_end85-ftl_free_no_use_map_blk
                                        // -- End function
	.globl	ftl_map_blk_alloc_new_blk       // -- Begin function ftl_map_blk_alloc_new_blk
	.p2align	2
	.type	ftl_map_blk_alloc_new_blk,@function
ftl_map_blk_alloc_new_blk:              // @ftl_map_blk_alloc_new_blk
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-64]!           // 16-byte Folded Spill
	ldrh	w8, [x0, #10]
	stp	x24, x23, [sp, #16]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #32]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #48]             // 16-byte Folded Spill
	mov	x29, sp
	cbz	x8, .LBB86_5
// %bb.1:
	ldr	x22, [x0, #16]
	mov	x19, x0
	mov	x21, xzr
.LBB86_2:                               // =>This Inner Loop Header: Depth=1
	ldrh	w9, [x22, x21, lsl #1]
	cbz	w9, .LBB86_7
// %bb.3:                               //   in Loop: Header=BB86_2 Depth=1
	add	x21, x21, #1                    // =1
	cmp	x21, x8
	b.lo	.LBB86_2
// %bb.4:
	cmp	w8, w21, uxth
	b.hi	.LBB86_6
.LBB86_5:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.ftl_map_blk_alloc_new_blk
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.ftl_map_blk_alloc_new_blk
	mov	w2, #578
	bl	sftl_printk
.LBB86_6:
	ldp	x20, x19, [sp, #48]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #32]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #64             // 16-byte Folded Reload
	hint	#29
	ret
.LBB86_7:
	adrp	x23, gSysFreeQueue+6
	ldrh	w8, [x23, :lo12:gSysFreeQueue+6]
	mov	w24, #65535
	cbz	w8, .LBB86_11
// %bb.8:
	adrp	x9, gSysFreeQueue
	add	x9, x9, :lo12:gSysFreeQueue
	ldrh	w10, [x9, #2]
	sub	w8, w8, #1                      // =1
	mov	w2, #1
	add	x11, x9, x10, lsl #1
	ldrh	w20, [x11, #8]
	strh	w8, [x9, #6]
	adrp	x8, req_erase
	ldr	x11, [x8, :lo12:req_erase]
	add	w10, w10, #1                    // =1
	and	w10, w10, #0x3ff
	strh	w10, [x9, #2]
	lsl	w9, w20, #10
	str	w9, [x11, #4]
	ldr	x0, [x8, :lo12:req_erase]
	bl	FlashEraseBlocks
	adrp	x8, g_totle_sys_slc_erase_count
	ldr	w9, [x8, :lo12:g_totle_sys_slc_erase_count]
	cmp	w20, w24
	add	w9, w9, #1                      // =1
	str	w9, [x8, :lo12:g_totle_sys_slc_erase_count]
	b.eq	.LBB86_12
// %bb.9:
	cbz	w20, .LBB86_12
// %bb.10:
	strh	w20, [x22, x21, lsl #1]
	ldr	w9, [x19, #48]
	ldrh	w10, [x19, #8]
	ldrh	w8, [x19, #10]
	strh	wzr, [x19, #2]
	add	w9, w9, #1                      // =1
	add	w10, w10, #1                    // =1
	strh	w21, [x19]
	str	w9, [x19, #48]
	strh	w10, [x19, #8]
	cmp	w8, w21, uxth
	b.ls	.LBB86_5
	b	.LBB86_6
.LBB86_11:
	mov	w20, #65535
.LBB86_12:
	ldrh	w2, [x23, :lo12:gSysFreeQueue+6]
	adrp	x0, .L.str.86
	add	x0, x0, :lo12:.L.str.86
	mov	w1, w20
	bl	sftl_printk
.LBB86_13:                              // =>This Inner Loop Header: Depth=1
	b	.LBB86_13
.Lfunc_end86:
	.size	ftl_map_blk_alloc_new_blk, .Lfunc_end86-ftl_map_blk_alloc_new_blk
                                        // -- End function
	.globl	ftl_map_blk_gc                  // -- Begin function ftl_map_blk_gc
	.p2align	2
	.type	ftl_map_blk_gc,@function
ftl_map_blk_gc:                         // @ftl_map_blk_gc
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-96]!           // 16-byte Folded Spill
	stp	x26, x25, [sp, #32]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #80]             // 16-byte Folded Spill
	ldr	x20, [x0, #16]
	ldr	x26, [x0, #40]
	stp	x28, x27, [sp, #16]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #48]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #64]             // 16-byte Folded Spill
	mov	x29, sp
	mov	x19, x0
	bl	ftl_free_no_use_map_blk
	ldrh	w8, [x19, #10]
	ldrh	w9, [x19, #8]
	adrp	x24, c_ftl_nand_page_pre_slc_blk
	sub	w8, w8, #5                      // =5
	cmp	w8, w9
	b.gt	.LBB87_20
// %bb.1:
	and	w8, w0, #0xffff
	ldrh	w25, [x20, w8, uxtw #1]
	cbz	w25, .LBB87_20
// %bb.2:
	ldr	w9, [x19, #52]
	cbnz	w9, .LBB87_20
// %bb.3:
	mov	w9, #1
	str	w9, [x19, #52]
	strh	wzr, [x20, x8, lsl #1]
	ldrh	w8, [x19, #8]
	sub	w8, w8, #1                      // =1
	strh	w8, [x19, #8]
	ldrh	w8, [x19, #2]
	ldrh	w9, [x24, :lo12:c_ftl_nand_page_pre_slc_blk]
	cmp	w8, w9
	b.lo	.LBB87_5
// %bb.4:
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.LBB87_5:
	ldrh	w8, [x19, #6]
	cbz	w8, .LBB87_15
// %bb.6:
	adrp	x21, req_sys
	mov	x20, xzr
	adrp	x27, p_sys_data_buf_1
	add	x21, x21, :lo12:req_sys
	adrp	x28, p_sys_spare_buf
	adrp	x22, req_sys
	b	.LBB87_8
.LBB87_7:                               //   in Loop: Header=BB87_8 Depth=1
	add	x20, x20, #1                    // =1
	cmp	x20, w8, uxth
	b.hs	.LBB87_15
.LBB87_8:                               // =>This Inner Loop Header: Depth=1
	ldr	w9, [x26, x20, lsl #2]
	cmp	w25, w9, lsr #10
	b.ne	.LBB87_7
// %bb.9:                               //   in Loop: Header=BB87_8 Depth=1
	ldr	x8, [x27, :lo12:p_sys_data_buf_1]
	ldr	x23, [x28, :lo12:p_sys_spare_buf]
	mov	w1, #1
	mov	x0, x21
	stp	x8, x23, [x21, #8]
	ldr	w8, [x26, x20, lsl #2]
	str	w8, [x21, #4]
	bl	FlashReadPages
	ldrh	w8, [x23, #8]
	cmp	x20, x8
	b.eq	.LBB87_11
// %bb.10:                              //   in Loop: Header=BB87_8 Depth=1
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.ftl_map_blk_gc
	mov	w2, #611
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.ftl_map_blk_gc
	bl	sftl_printk
.LBB87_11:                              //   in Loop: Header=BB87_8 Depth=1
	ldr	w8, [x22, :lo12:req_sys]
	cmn	w8, #1                          // =1
	b.eq	.LBB87_23
// %bb.12:                              //   in Loop: Header=BB87_8 Depth=1
	ldrh	w8, [x23, #8]
	cmp	x20, x8
	b.ne	.LBB87_23
// %bb.13:                              //   in Loop: Header=BB87_8 Depth=1
	ldrh	w8, [x23]
	ldrh	w9, [x19, #4]
	cmp	w8, w9
	b.ne	.LBB87_23
// %bb.14:                              //   in Loop: Header=BB87_8 Depth=1
	adrp	x8, req_sys+8
	ldr	x2, [x8, :lo12:req_sys+8]
	mov	x0, x19
	mov	w1, w20
	bl	FtlMapWritePage
	ldrh	w8, [x19, #6]
	b	.LBB87_7
.LBB87_15:
	cbz	w25, .LBB87_19
// %bb.16:
	mov	w8, #65535
	cmp	w25, w8
	b.eq	.LBB87_19
// %bb.17:
	adrp	x8, gSysFreeQueue+6
	ldrh	w8, [x8, :lo12:gSysFreeQueue+6]
	cmp	w8, #1024                       // =1024
	b.eq	.LBB87_19
// %bb.18:
	adrp	x9, req_erase
	ldr	x10, [x9, :lo12:req_erase]
	adrp	x8, c_ftl_nand_blks_per_die
	ldrh	w8, [x8, :lo12:c_ftl_nand_blks_per_die]
	adrp	x11, c_ftl_nand_planes_per_die
	lsl	w12, w25, #10
	ldrh	w11, [x11, :lo12:c_ftl_nand_planes_per_die]
	str	w12, [x10, #4]
	ldr	x0, [x9, :lo12:req_erase]
	udiv	w9, w25, w8
	msub	w8, w9, w8, w25
	mov	w2, #1
	udiv	w20, w8, w11
	bl	FlashEraseBlocks
	adrp	x8, p_erase_count_table
	ldr	x8, [x8, :lo12:p_erase_count_table]
	lsl	x9, x20, #1
	adrp	x11, g_totle_sys_slc_erase_count
	ldrh	w10, [x8, x9]
	add	w10, w10, #1                    // =1
	strh	w10, [x8, x9]
	adrp	x9, gSysFreeQueue
	ldr	w8, [x11, :lo12:g_totle_sys_slc_erase_count]
	add	x9, x9, :lo12:gSysFreeQueue
	ldrh	w10, [x9, #6]
	ldrh	w12, [x9, #4]
	add	w8, w8, #1                      // =1
	str	w8, [x11, :lo12:g_totle_sys_slc_erase_count]
	add	w8, w10, #1                     // =1
	strh	w8, [x9, #6]
	add	x8, x9, x12, lsl #1
	add	w10, w12, #1                    // =1
	strh	w25, [x8, #8]
	and	w8, w10, #0x3ff
	strh	w8, [x9, #4]
.LBB87_19:
	str	wzr, [x19, #52]
.LBB87_20:
	ldrh	w8, [x19, #2]
	ldrh	w9, [x24, :lo12:c_ftl_nand_page_pre_slc_blk]
	cmp	w8, w9
	b.lo	.LBB87_22
// %bb.21:
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.LBB87_22:
	ldp	x20, x19, [sp, #80]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #64]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #48]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #32]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #96             // 16-byte Folded Reload
	hint	#29
	ret
.LBB87_23:
	str	wzr, [x26, x20, lsl #2]
.LBB87_24:                              // =>This Inner Loop Header: Depth=1
	b	.LBB87_24
.Lfunc_end87:
	.size	ftl_map_blk_gc, .Lfunc_end87-ftl_map_blk_gc
                                        // -- End function
	.globl	Ftl_write_map_blk_to_last_page  // -- Begin function Ftl_write_map_blk_to_last_page
	.p2align	2
	.type	Ftl_write_map_blk_to_last_page,@function
Ftl_write_map_blk_to_last_page:         // @Ftl_write_map_blk_to_last_page
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-64]!           // 16-byte Folded Spill
	ldrh	w8, [x0]
	stp	x22, x21, [sp, #32]             // 16-byte Folded Spill
	ldr	x22, [x0, #16]
	mov	w9, #65535
	stp	x20, x19, [sp, #48]             // 16-byte Folded Spill
	mov	x19, x0
	cmp	x8, x9
	str	x23, [sp, #16]                  // 8-byte Folded Spill
	mov	x29, sp
	b.eq	.LBB88_11
// %bb.1:
	ldrh	w22, [x22, x8, lsl #1]
	ldrh	w8, [x19, #2]
	adrp	x23, p_sys_data_buf
	adrp	x10, p_sys_spare_buf
	ldr	x11, [x23, :lo12:p_sys_data_buf]
	ldr	x20, [x10, :lo12:p_sys_spare_buf]
	adrp	x9, req_sys+4
	add	x9, x9, :lo12:req_sys+4
	orr	w8, w8, w22, lsl #10
	ldr	x21, [x19, #40]
	str	w8, [x9]
	stur	x11, [x9, #4]
	stur	x20, [x9, #12]
	ldr	w8, [x19, #48]
	mov	w9, #64245
	strh	w9, [x20, #8]
	mov	w1, #255
	str	w8, [x20, #4]
	ldrh	w8, [x19, #4]
	strh	w22, [x20, #2]
	strh	w8, [x20]
	adrp	x8, c_ftl_nand_page_pre_slc_blk
	ldrh	w8, [x8, :lo12:c_ftl_nand_page_pre_slc_blk]
	ldr	x0, [x23, :lo12:p_sys_data_buf]
	lsl	x2, x8, #3
	bl	memset
	ldrh	w10, [x19, #6]
	cbz	w10, .LBB88_6
// %bb.2:
	mov	x8, xzr
	mov	w9, wzr
	b	.LBB88_4
.LBB88_3:                               //   in Loop: Header=BB88_4 Depth=1
	add	x8, x8, #1                      // =1
	cmp	x8, w10, uxth
	b.hs	.LBB88_6
.LBB88_4:                               // =>This Inner Loop Header: Depth=1
	ldr	w11, [x21, x8, lsl #2]
	cmp	w22, w11, lsr #10
	b.ne	.LBB88_3
// %bb.5:                               //   in Loop: Header=BB88_4 Depth=1
	ldr	x10, [x23, :lo12:p_sys_data_buf]
	add	w9, w9, #1                      // =1
	and	w11, w9, #0xffff
	lsl	w12, w11, #1
	str	w8, [x10, w12, uxtw #2]
	ldr	w10, [x21, x8, lsl #2]
	ldr	x12, [x23, :lo12:p_sys_data_buf]
	mov	w13, #1
	bfi	w13, w11, #1, #16
	str	w10, [x12, w13, uxtw #2]
	ldrh	w10, [x19, #6]
	b	.LBB88_3
.LBB88_6:
	adrp	x8, c_ftl_nand_byte_pre_page
	ldrh	w9, [x8, :lo12:c_ftl_nand_byte_pre_page]
	mov	w8, #42982
	movk	w8, #18374, lsl #16
	cbz	x9, .LBB88_9
// %bb.7:
	adrp	x10, req_sys+8
	ldr	x10, [x10, :lo12:req_sys+8]
.LBB88_8:                               // =>This Inner Loop Header: Depth=1
	ldrb	w11, [x10], #1
	lsr	w12, w8, #2
	add	w12, w12, w8, lsl #5
	subs	x9, x9, #1                      // =1
	add	w11, w12, w11
	eor	w8, w11, w8
	b.ne	.LBB88_8
.LBB88_9:
	adrp	x0, req_sys
	add	x0, x0, :lo12:req_sys
	mov	w1, #1
	mov	w2, #1
	str	w8, [x20, #12]
	bl	FlashProgPages
	ldrh	w8, [x19, #2]
	mov	x0, x19
	add	w8, w8, #1                      // =1
	strh	w8, [x19, #2]
	bl	ftl_map_blk_gc
.LBB88_10:
	ldp	x20, x19, [sp, #48]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #32]             // 16-byte Folded Reload
	ldr	x23, [sp, #16]                  // 8-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #64             // 16-byte Folded Reload
	hint	#29
	ret
.LBB88_11:
	ldrh	w8, [x19, #8]
	cbz	w8, .LBB88_13
// %bb.12:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.Ftl_write_map_blk_to_last_page
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.Ftl_write_map_blk_to_last_page
	mov	w2, #641
	bl	sftl_printk
	ldrh	w8, [x19, #8]
	add	w8, w8, #1                      // =1
	b	.LBB88_14
.LBB88_13:
	mov	w8, #1
.LBB88_14:
	strh	w8, [x19, #8]
	adrp	x21, gSysFreeQueue+6
	ldrh	w8, [x21, :lo12:gSysFreeQueue+6]
	mov	w23, #65535
	cbz	w8, .LBB88_18
// %bb.15:
	adrp	x9, gSysFreeQueue
	add	x9, x9, :lo12:gSysFreeQueue
	ldrh	w10, [x9, #2]
	sub	w8, w8, #1                      // =1
	mov	w2, #1
	add	x11, x9, x10, lsl #1
	ldrh	w20, [x11, #8]
	strh	w8, [x9, #6]
	adrp	x8, req_erase
	ldr	x11, [x8, :lo12:req_erase]
	add	w10, w10, #1                    // =1
	and	w10, w10, #0x3ff
	strh	w10, [x9, #2]
	lsl	w9, w20, #10
	str	w9, [x11, #4]
	ldr	x0, [x8, :lo12:req_erase]
	bl	FlashEraseBlocks
	adrp	x8, g_totle_sys_slc_erase_count
	ldr	w9, [x8, :lo12:g_totle_sys_slc_erase_count]
	cmp	w20, w23
	add	w9, w9, #1                      // =1
	str	w9, [x8, :lo12:g_totle_sys_slc_erase_count]
	b.eq	.LBB88_19
// %bb.16:
	cbz	w20, .LBB88_19
// %bb.17:
	strh	w20, [x22]
	ldr	w8, [x19, #48]
	str	wzr, [x19]
	add	w8, w8, #1                      // =1
	str	w8, [x19, #48]
	b	.LBB88_10
.LBB88_18:
	mov	w20, #65535
.LBB88_19:
	ldrh	w2, [x21, :lo12:gSysFreeQueue+6]
	adrp	x0, .L.str.86
	add	x0, x0, :lo12:.L.str.86
	mov	w1, w20
	bl	sftl_printk
.LBB88_20:                              // =>This Inner Loop Header: Depth=1
	b	.LBB88_20
.Lfunc_end88:
	.size	Ftl_write_map_blk_to_last_page, .Lfunc_end88-Ftl_write_map_blk_to_last_page
                                        // -- End function
	.globl	js_hash                         // -- Begin function js_hash
	.p2align	2
	.type	js_hash,@function
js_hash:                                // @js_hash
// %bb.0:
	hint	#25
	mov	x8, x0
	mov	w0, #42982
	movk	w0, #18374, lsl #16
	cbz	w1, .LBB89_3
// %bb.1:
	mov	w9, w1
.LBB89_2:                               // =>This Inner Loop Header: Depth=1
	ldrb	w10, [x8], #1
	lsr	w11, w0, #2
	add	w11, w11, w0, lsl #5
	subs	x9, x9, #1                      // =1
	add	w10, w11, w10
	eor	w0, w10, w0
	b.ne	.LBB89_2
.LBB89_3:
	hint	#29
	ret
.Lfunc_end89:
	.size	js_hash, .Lfunc_end89-js_hash
                                        // -- End function
	.globl	flush_l2p_region                // -- Begin function flush_l2p_region
	.p2align	2
	.type	flush_l2p_region,@function
flush_l2p_region:                       // @flush_l2p_region
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #16]             // 16-byte Folded Spill
	adrp	x19, p_l2p_ram_map
	ldr	x8, [x19, :lo12:p_l2p_ram_map]
                                        // kill: def $w0 killed $w0 def $x0
	and	x9, x0, #0xffff
	lsl	x20, x9, #4
	adrp	x0, gL2pMapInfo
	add	x8, x8, x20
	ldrh	w1, [x8]
	ldr	x2, [x8, #8]
	add	x0, x0, :lo12:gL2pMapInfo
	mov	x29, sp
	bl	FtlMapWritePage
	ldr	x8, [x19, :lo12:p_l2p_ram_map]
	mov	w0, wzr
	add	x8, x8, x20
	ldr	w9, [x8, #4]
	ldp	x20, x19, [sp, #16]             // 16-byte Folded Reload
	and	w9, w9, #0x7fffffff
	str	w9, [x8, #4]
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end90:
	.size	flush_l2p_region, .Lfunc_end90-flush_l2p_region
                                        // -- End function
	.globl	select_l2p_ram_region           // -- Begin function select_l2p_ram_region
	.p2align	2
	.type	select_l2p_ram_region,@function
select_l2p_ram_region:                  // @select_l2p_ram_region
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	adrp	x8, c_ftl_nand_l2pmap_ram_region_num
	ldrh	w8, [x8, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	str	x19, [sp, #16]                  // 8-byte Folded Spill
	mov	x29, sp
	cbz	x8, .LBB91_11
// %bb.1:
	adrp	x9, p_l2p_ram_map
	ldr	x10, [x9, :lo12:p_l2p_ram_map]
	mov	x19, xzr
.LBB91_2:                               // =>This Inner Loop Header: Depth=1
	ldrsh	w11, [x10]
	cmn	w11, #1                         // =1
	b.eq	.LBB91_14
// %bb.3:                               //   in Loop: Header=BB91_2 Depth=1
	add	x19, x19, #1                    // =1
	cmp	x8, x19
	add	x10, x10, #16                   // =16
	b.ne	.LBB91_2
// %bb.4:
	ldr	x11, [x9, :lo12:p_l2p_ram_map]
	mov	x10, xzr
	mov	w12, #-2147483648
	mov	w19, w8
	add	x11, x11, #4                    // =4
.LBB91_5:                               // =>This Inner Loop Header: Depth=1
	ldr	w13, [x11], #16
	cmp	w13, #0                         // =0
	cset	w14, ge
	cmp	w13, w12
	cset	w15, lo
	tst	w14, w15
	csel	w19, w10, w19, ne
	add	x10, x10, #1                    // =1
	csel	w12, w13, w12, ne
	cmp	x8, x10
	b.ne	.LBB91_5
// %bb.6:
	cmp	w8, w19, uxth
	b.hi	.LBB91_14
// %bb.7:
	ldr	x11, [x9, :lo12:p_l2p_ram_map]
	adrp	x9, g_l2p_last_update_region_id
	ldrh	w9, [x9, :lo12:g_l2p_last_update_region_id]
	mov	x10, xzr
	add	x11, x11, #4                    // =4
	mov	w12, #-1
	mov	w19, w8
	b	.LBB91_9
.LBB91_8:                               //   in Loop: Header=BB91_9 Depth=1
	add	x10, x10, #1                    // =1
	cmp	x8, x10
	add	x11, x11, #16                   // =16
	b.eq	.LBB91_12
.LBB91_9:                               // =>This Inner Loop Header: Depth=1
	ldr	w13, [x11]
	cmp	w13, w12
	b.hs	.LBB91_8
// %bb.10:                              //   in Loop: Header=BB91_9 Depth=1
	ldurh	w14, [x11, #-4]
	cmp	w9, w14
	csel	w19, w19, w10, eq
	csel	w12, w12, w13, eq
	b	.LBB91_8
.LBB91_11:
	mov	w19, wzr
	b	.LBB91_13
.LBB91_12:
	cmp	w8, w19, uxth
	b.hi	.LBB91_14
.LBB91_13:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.select_l2p_ram_region
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.select_l2p_ram_region
	mov	w2, #796
	bl	sftl_printk
.LBB91_14:
	mov	w0, w19
	ldr	x19, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end91:
	.size	select_l2p_ram_region, .Lfunc_end91-select_l2p_ram_region
                                        // -- End function
	.globl	FtlWriteDump_data               // -- Begin function FtlWriteDump_data
	.p2align	2
	.type	FtlWriteDump_data,@function
FtlWriteDump_data:                      // @FtlWriteDump_data
// %bb.0:
	hint	#25
	sub	sp, sp, #112                    // =112
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	stp	x29, x30, [sp, #48]             // 16-byte Folded Spill
	add	x29, sp, #48                    // =48
	adrp	x9, g_active_superblock+4
	stp	x24, x23, [sp, #64]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #80]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #96]             // 16-byte Folded Spill
	stur	x8, [x29, #-8]
	add	x9, x9, :lo12:g_active_superblock+4
	ldrh	w8, [x9]
	cbz	w8, .LBB92_3
// %bb.1:
	ldrb	w9, [x9, #4]
	cbnz	w9, .LBB92_3
// %bb.2:
	adrp	x9, g_active_superblock+7
	adrp	x10, c_ftl_nand_page_pre_blk
	ldrb	w9, [x9, :lo12:g_active_superblock+7]
	ldrh	w10, [x10, :lo12:c_ftl_nand_page_pre_blk]
	mul	w9, w10, w9
	cmp	w9, w8
	b.ne	.LBB92_6
.LBB92_3:
	adrp	x8, g_active_superblock+10
	strb	wzr, [x8, :lo12:g_active_superblock+10]
.LBB92_4:
	adrp	x9, __stack_chk_guard
	ldur	x8, [x29, #-8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB92_16
// %bb.5:
	ldp	x20, x19, [sp, #96]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #80]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #64]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #48]             // 16-byte Folded Reload
	add	sp, sp, #112                    // =112
	hint	#29
	ret
.LBB92_6:
	adrp	x21, g_active_superblock+10
	ldrb	w8, [x21, :lo12:g_active_superblock+10]
	cbnz	w8, .LBB92_4
// %bb.7:
	adrp	x8, g_MaxLpn
	ldr	w8, [x8, :lo12:g_MaxLpn]
	adrp	x9, c_ftl_nand_planes_num
	ldrh	w20, [x9, :lo12:c_ftl_nand_planes_num]
	sub	x1, x29, #12                    // =12
	sub	w19, w8, #1                     // =1
	mov	w0, w19
	mov	w2, wzr
	bl	log2phys
	ldur	w9, [x29, #-12]
	adrp	x8, p_sys_data_buf
	adrp	x10, p_sys_spare_buf
	ldr	x11, [x8, :lo12:p_sys_data_buf]
	ldr	x22, [x10, :lo12:p_sys_spare_buf]
	cmn	w9, #1                          // =1
	str	w19, [sp, #24]
	str	w9, [sp, #4]
	stp	x11, x22, [sp, #8]
	str	wzr, [x22, #4]
	b.eq	.LBB92_9
// %bb.8:
	mov	x0, sp
	mov	w1, #1
	bl	FlashReadPages
	b	.LBB92_10
.LBB92_9:
	ldr	x0, [x8, :lo12:p_sys_data_buf]
	adrp	x8, c_ftl_nand_byte_pre_page
	ldrh	w2, [x8, :lo12:c_ftl_nand_byte_pre_page]
	mov	w1, #255
	bl	memset
.LBB92_10:
	mov	w8, #61589
	strh	w8, [x22]
	cbz	w20, .LBB92_15
// %bb.11:
	adrp	x8, g_active_superblock+4
	ldrh	w8, [x8, :lo12:g_active_superblock+4]
	cbz	w8, .LBB92_15
// %bb.12:
	lsl	w8, w20, #2
	mov	w9, #1
	adrp	x20, g_active_superblock
	add	x20, x20, :lo12:g_active_superblock
	sub	w23, w9, w8
	adrp	x24, g_GlobalDataVersion
.LBB92_13:                              // =>This Inner Loop Header: Depth=1
	ldr	w8, [sp, #4]
	mov	x0, x20
	stp	w19, w8, [x22, #8]
	ldrh	w8, [x20]
	strh	w8, [x22, #2]
	bl	get_new_active_ppa
	ldr	w8, [x24, :lo12:g_GlobalDataVersion]
	str	w0, [sp, #4]
	mov	x0, sp
	mov	w1, #1
	add	w9, w8, #1                      // =1
	cmn	w9, #1                          // =1
	str	w8, [x22, #4]
	csinc	w8, wzr, w8, eq
	mov	w2, wzr
	str	w8, [x24, :lo12:g_GlobalDataVersion]
	bl	FlashProgPages
	ldrh	w0, [x20]
	bl	decrement_vpc_count
	cbz	w23, .LBB92_15
// %bb.14:                              //   in Loop: Header=BB92_13 Depth=1
	ldrh	w8, [x20, #4]
	add	w23, w23, #1                    // =1
	cbnz	w8, .LBB92_13
.LBB92_15:
	mov	w8, #1
	strb	w8, [x21, :lo12:g_active_superblock+10]
	b	.LBB92_4
.LBB92_16:
	bl	__stack_chk_fail
.Lfunc_end92:
	.size	FtlWriteDump_data, .Lfunc_end92-FtlWriteDump_data
                                        // -- End function
	.globl	Ftl_save_ext_data               // -- Begin function Ftl_save_ext_data
	.p2align	2
	.type	Ftl_save_ext_data,@function
Ftl_save_ext_data:                      // @Ftl_save_ext_data
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	adrp	x8, g_sys_ext_data
	ldr	w8, [x8, :lo12:g_sys_ext_data]
	mov	w9, #19539
	movk	w9, #18004, lsl #16
	mov	x29, sp
	cmp	w8, w9
	b.ne	.LBB93_2
// %bb.1:
	adrp	x9, g_totle_write_sector
	adrp	x10, g_totle_read_sector
	ldr	w9, [x9, :lo12:g_totle_write_sector]
	ldr	w10, [x10, :lo12:g_totle_read_sector]
	adrp	x2, g_sys_ext_data
	add	x2, x2, :lo12:g_sys_ext_data
	adrp	x11, g_totle_gc_page_count
	adrp	x12, g_totle_write_page_count
	adrp	x13, g_totle_read_page_count
	stp	w9, w10, [x2, #88]
	ldr	w10, [x11, :lo12:g_totle_gc_page_count]
	ldr	w11, [x12, :lo12:g_totle_write_page_count]
	ldr	w12, [x13, :lo12:g_totle_read_page_count]
	mov	w8, #87
	movk	w8, #20480, lsl #16
	adrp	x14, g_totle_l2p_write_count
	adrp	x15, g_totle_slc_erase_count
	adrp	x9, g_totle_sys_slc_erase_count
	stp	w8, w10, [x2, #4]
	ldr	w10, [x14, :lo12:g_totle_l2p_write_count]
	stp	w11, w12, [x2, #12]
	ldr	w12, [x15, :lo12:g_totle_slc_erase_count]
	ldr	w9, [x9, :lo12:g_totle_sys_slc_erase_count]
	adrp	x8, g_totle_discard_page_count
	adrp	x11, g_totle_cache_write_count
	str	w10, [x2, #20]
	adrp	x10, g_max_erase_count
	stp	w12, w9, [x2, #28]
	adrp	x9, g_min_erase_count
	ldr	w8, [x8, :lo12:g_totle_discard_page_count]
	ldr	w11, [x11, :lo12:g_totle_cache_write_count]
	ldr	w10, [x10, :lo12:g_max_erase_count]
	ldr	w9, [x9, :lo12:g_min_erase_count]
	mov	w1, #1
	mov	w0, wzr
	stp	w8, w11, [x2, #36]
	stp	w10, w9, [x2, #44]
	bl	FtlVendorPartWrite
.LBB93_2:
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end93:
	.size	Ftl_save_ext_data, .Lfunc_end93-Ftl_save_ext_data
                                        // -- End function
	.globl	FtlUpdateVaildLpn               // -- Begin function FtlUpdateVaildLpn
	.p2align	2
	.type	FtlUpdateVaildLpn,@function
FtlUpdateVaildLpn:                      // @FtlUpdateVaildLpn
// %bb.0:
	hint	#25
	adrp	x9, FtlUpdateVaildLpnCount
	ldrh	w8, [x9, :lo12:FtlUpdateVaildLpnCount]
	add	w10, w8, #1                     // =1
	strh	w10, [x9, :lo12:FtlUpdateVaildLpnCount]
	cbnz	w0, .LBB94_2
// %bb.1:
	cmp	w8, #5                          // =5
	b.lo	.LBB94_7
.LBB94_2:
	adrp	x8, c_ftl_nand_data_blks_per_plane
	ldrh	w8, [x8, :lo12:c_ftl_nand_data_blks_per_plane]
	strh	wzr, [x9, :lo12:FtlUpdateVaildLpnCount]
	adrp	x9, g_VaildLpn
	str	wzr, [x9, :lo12:g_VaildLpn]
	cbz	x8, .LBB94_7
// %bb.3:
	adrp	x11, p_valid_page_count_table
	ldr	x11, [x11, :lo12:p_valid_page_count_table]
	mov	w10, wzr
	mov	w12, #65535
	b	.LBB94_5
.LBB94_4:                               //   in Loop: Header=BB94_5 Depth=1
	subs	x8, x8, #1                      // =1
	add	x11, x11, #2                    // =2
	b.eq	.LBB94_7
.LBB94_5:                               // =>This Inner Loop Header: Depth=1
	ldrh	w13, [x11]
	cmp	w13, w12
	b.eq	.LBB94_4
// %bb.6:                               //   in Loop: Header=BB94_5 Depth=1
	add	w10, w10, w13
	str	w10, [x9, :lo12:g_VaildLpn]
	b	.LBB94_4
.LBB94_7:
	hint	#29
	ret
.Lfunc_end94:
	.size	FtlUpdateVaildLpn, .Lfunc_end94-FtlUpdateVaildLpn
                                        // -- End function
	.globl	FtlMapBlkWriteDump_data         // -- Begin function FtlMapBlkWriteDump_data
	.p2align	2
	.type	FtlMapBlkWriteDump_data,@function
FtlMapBlkWriteDump_data:                // @FtlMapBlkWriteDump_data
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	ldr	w8, [x0, #56]
	stp	x20, x19, [sp, #16]             // 16-byte Folded Spill
	mov	x29, sp
	cbz	w8, .LBB95_5
// %bb.1:
	ldrh	w8, [x0, #6]
	str	wzr, [x0, #56]
	adrp	x10, p_sys_data_buf_1
	adrp	x11, p_sys_spare_buf
	mov	x19, x0
	ldr	x9, [x0, #40]
	ldr	x0, [x10, :lo12:p_sys_data_buf_1]
	ldr	x10, [x11, :lo12:p_sys_spare_buf]
	adrp	x11, req_sys+4
	add	x11, x11, :lo12:req_sys+4
	sub	w20, w8, #1                     // =1
	stur	x0, [x11, #4]
	stur	x10, [x11, #12]
	and	x8, x20, #0xffff
	ldr	w8, [x9, x8, lsl #2]
	str	w8, [x11]
	cbz	w8, .LBB95_3
// %bb.2:
	adrp	x0, req_sys
	add	x0, x0, :lo12:req_sys
	mov	w1, #1
	bl	FlashReadPages
	b	.LBB95_4
.LBB95_3:
	adrp	x8, c_ftl_nand_byte_pre_page
	ldrh	w2, [x8, :lo12:c_ftl_nand_byte_pre_page]
	mov	w1, #255
	bl	memset
.LBB95_4:
	adrp	x8, req_sys+8
	ldr	x2, [x8, :lo12:req_sys+8]
	and	w1, w20, #0xffff
	mov	x0, x19
	bl	FtlMapWritePage
.LBB95_5:
	ldp	x20, x19, [sp, #16]             // 16-byte Folded Reload
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end95:
	.size	FtlMapBlkWriteDump_data, .Lfunc_end95-FtlMapBlkWriteDump_data
                                        // -- End function
	.globl	FtlScanSysBlk                   // -- Begin function FtlScanSysBlk
	.p2align	2
	.type	FtlScanSysBlk,@function
FtlScanSysBlk:                          // @FtlScanSysBlk
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-96]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #80]             // 16-byte Folded Spill
	adrp	x8, g_totle_map_block
	adrp	x19, c_ftl_nand_max_map_blks
	adrp	x9, p_map_block_ver_table
	strh	wzr, [x8, :lo12:g_totle_map_block]
	ldr	w8, [x19, :lo12:c_ftl_nand_max_map_blks]
	ldr	x0, [x9, :lo12:p_map_block_ver_table]
	adrp	x9, g_totle_vendor_block
	mov	w1, wzr
	lsl	w2, w8, #2
	stp	x28, x27, [sp, #16]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #32]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #48]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #64]             // 16-byte Folded Spill
	mov	x29, sp
	strh	wzr, [x9, :lo12:g_totle_vendor_block]
	bl	memset
	adrp	x9, p_map_block_table
	ldr	w8, [x19, :lo12:c_ftl_nand_max_map_blks]
	ldr	x0, [x9, :lo12:p_map_block_table]
	mov	w1, wzr
	lsl	w2, w8, #1
	bl	memset
	adrp	x19, c_ftl_nand_max_vendor_blks
	adrp	x9, p_vendor_block_ver_table
	ldrh	w8, [x19, :lo12:c_ftl_nand_max_vendor_blks]
	ldr	x0, [x9, :lo12:p_vendor_block_ver_table]
	mov	w1, wzr
	lsl	x2, x8, #2
	bl	memset
	adrp	x9, p_vendor_block_table
	ldrh	w8, [x19, :lo12:c_ftl_nand_max_vendor_blks]
	ldr	x0, [x9, :lo12:p_vendor_block_table]
	mov	w1, wzr
	lsl	x2, x8, #1
	bl	memset
	adrp	x8, c_ftl_nand_data_blks_per_plane
	adrp	x9, c_ftl_nand_blk_pre_plane
	ldrh	w19, [x8, :lo12:c_ftl_nand_data_blks_per_plane]
	ldrh	w8, [x9, :lo12:c_ftl_nand_blk_pre_plane]
	adrp	x10, gSysInfo
	add	x10, x10, :lo12:gSysInfo
	mov	x9, #-1
	cmp	w19, w8
	stp	x9, x9, [x10]
	b.hs	.LBB96_80
// %bb.1:
	adrp	x8, c_ftl_nand_planes_num
	adrp	x21, c_ftl_nand_planes_per_die
	adrp	x20, c_ftl_nand_blks_per_die
	adrp	x22, req_read
	b	.LBB96_3
.LBB96_2:                               //   in Loop: Header=BB96_3 Depth=1
	adrp	x8, c_ftl_nand_blk_pre_plane
	ldrh	w8, [x8, :lo12:c_ftl_nand_blk_pre_plane]
	add	w19, w19, #1                    // =1
	cmp	w8, w19, uxth
	adrp	x8, c_ftl_nand_planes_num
	b.ls	.LBB96_80
.LBB96_3:                               // =>This Loop Header: Depth=1
                                        //     Child Loop BB96_6 Depth 2
                                        //     Child Loop BB96_12 Depth 2
                                        //       Child Loop BB96_48 Depth 3
                                        //       Child Loop BB96_32 Depth 3
                                        //       Child Loop BB96_40 Depth 3
                                        //       Child Loop BB96_59 Depth 3
                                        //       Child Loop BB96_67 Depth 3
	ldrh	w9, [x8, :lo12:c_ftl_nand_planes_num]
	cbz	w9, .LBB96_2
// %bb.4:                               //   in Loop: Header=BB96_3 Depth=1
	adrp	x16, p_plane_order_table
	adrp	x17, gBbtInfo
	mov	x8, xzr
	mov	w25, wzr
	adrp	x15, c_ftl_nand_planes_num
	add	x16, x16, :lo12:p_plane_order_table
	add	x17, x17, :lo12:gBbtInfo
	adrp	x18, p_gc_data_buf
	adrp	x0, p_gc_spare_buf
	adrp	x1, c_ftl_nand_byte_pre_oob
	b	.LBB96_6
.LBB96_5:                               //   in Loop: Header=BB96_6 Depth=2
	add	x8, x8, #1                      // =1
	cmp	x8, w9, uxth
	b.hs	.LBB96_8
.LBB96_6:                               //   Parent Loop BB96_3 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldrb	w10, [x16, x8]
	ldrh	w11, [x21, :lo12:c_ftl_nand_planes_per_die]
	ldrh	w12, [x20, :lo12:c_ftl_nand_blks_per_die]
	and	w13, w10, #0xffff
	and	w14, w11, #0xffff
	udiv	w13, w13, w14
	mul	w14, w11, w19
	madd	w14, w12, w13, w14
	msub	w10, w13, w11, w10
	add	w11, w14, w10
	and	w10, w11, #0xffff
	and	w13, w12, #0xffff
	udiv	w13, w10, w13
	add	x14, x17, w13, uxtw #3
	ldr	x14, [x14, #32]
	msub	w11, w13, w12, w11
	lsr	w12, w11, #3
	and	x12, x12, #0x1ffc
	ldr	w12, [x14, x12]
	lsr	w11, w12, w11
	tbnz	w11, #0, .LBB96_5
// %bb.7:                               //   in Loop: Header=BB96_6 Depth=2
	ldr	x9, [x22, :lo12:req_read]
	and	x11, x25, #0xffff
	lsl	x11, x11, #5
	lsl	w10, w10, #10
	add	x9, x9, x11
	str	w10, [x9, #4]
	ldr	x9, [x22, :lo12:req_read]
	ldr	x10, [x18, :lo12:p_gc_data_buf]
	add	x9, x9, x11
	str	x10, [x9, #8]
	ldrh	w9, [x1, :lo12:c_ftl_nand_byte_pre_oob]
	and	w10, w25, #0xffff
	ldr	x12, [x0, :lo12:p_gc_spare_buf]
	ldr	x13, [x22, :lo12:req_read]
	mul	w9, w9, w10
	add	w10, w9, #3                     // =3
	cmp	w9, #0                          // =0
	csel	w9, w10, w9, lt
	asr	w9, w9, #2
	add	x9, x12, w9, sxtw #2
	add	x10, x13, x11
	str	x9, [x10, #16]
	ldrh	w9, [x15, :lo12:c_ftl_nand_planes_num]
	add	w25, w25, #1                    // =1
	b	.LBB96_5
.LBB96_8:                               //   in Loop: Header=BB96_3 Depth=1
	tst	w25, #0xffff
	b.eq	.LBB96_2
// %bb.9:                               //   in Loop: Header=BB96_3 Depth=1
	ldr	x0, [x22, :lo12:req_read]
	and	w1, w25, #0xffff
	bl	FlashReadPages
	mov	x23, xzr
                                        // kill: def $w25 killed $w25 killed $x25 def $x25
	and	x28, x25, #0xffff
	b	.LBB96_12
.LBB96_10:                              //   in Loop: Header=BB96_12 Depth=2
	lsr	w10, w27, #10
	strh	w10, [x9, x8, lsl #1]
.LBB96_11:                              //   in Loop: Header=BB96_12 Depth=2
	add	x23, x23, #1                    // =1
	cmp	x23, x28
	b.eq	.LBB96_2
.LBB96_12:                              //   Parent Loop BB96_3 Depth=1
                                        // =>  This Loop Header: Depth=2
                                        //       Child Loop BB96_48 Depth 3
                                        //       Child Loop BB96_32 Depth 3
                                        //       Child Loop BB96_40 Depth 3
                                        //       Child Loop BB96_59 Depth 3
                                        //       Child Loop BB96_67 Depth 3
	ldr	x8, [x22, :lo12:req_read]
	add	x9, x8, x23, lsl #5
	ldp	w10, w27, [x9]
	ldr	x25, [x9, #16]
	cmn	w10, #1                         // =1
	b.eq	.LBB96_46
.LBB96_13:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x8, g_GlobalSysVersion
	ldr	w9, [x8, :lo12:g_GlobalSysVersion]
	ldr	w8, [x25, #4]
	cmn	w9, #1                          // =1
	b.eq	.LBB96_15
// %bb.14:                              //   in Loop: Header=BB96_12 Depth=2
	cmp	w9, w8
	b.hi	.LBB96_17
.LBB96_15:                              //   in Loop: Header=BB96_12 Depth=2
	cmn	w8, #1                          // =1
	b.eq	.LBB96_17
// %bb.16:                              //   in Loop: Header=BB96_12 Depth=2
	add	w9, w8, #1                      // =1
	adrp	x10, g_GlobalSysVersion
	str	w9, [x10, :lo12:g_GlobalSysVersion]
.LBB96_17:                              //   in Loop: Header=BB96_12 Depth=2
	ldrh	w9, [x25]
	mov	w10, #61633
	cmp	w9, w10
	b.gt	.LBB96_27
// %bb.18:                              //   in Loop: Header=BB96_12 Depth=2
	mov	w10, #61574
	cmp	w9, w10
	b.eq	.LBB96_56
// %bb.19:                              //   in Loop: Header=BB96_12 Depth=2
	mov	w10, #61604
	cmp	w9, w10
	b.ne	.LBB96_11
// %bb.20:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x9, gSysInfo
	ldrsh	w9, [x9, :lo12:gSysInfo]
	cmn	w9, #1                          // =1
	b.eq	.LBB96_77
// %bb.21:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x9, gSysInfo+4
	ldrh	w24, [x9, :lo12:gSysInfo+4]
	mov	w9, #65535
	cmp	w24, w9
	b.eq	.LBB96_25
// %bb.22:                              //   in Loop: Header=BB96_12 Depth=2
	cbz	w24, .LBB96_25
// %bb.23:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x9, gSysFreeQueue+6
	ldrh	w9, [x9, :lo12:gSysFreeQueue+6]
	cmp	w9, #1024                       // =1024
	b.eq	.LBB96_25
// %bb.24:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x12, req_erase
	ldr	x8, [x12, :lo12:req_erase]
	ldrh	w9, [x20, :lo12:c_ftl_nand_blks_per_die]
	lsl	w11, w24, #10
	ldrh	w10, [x21, :lo12:c_ftl_nand_planes_per_die]
	str	w11, [x8, #4]
	ldr	x0, [x12, :lo12:req_erase]
	udiv	w8, w24, w9
	msub	w8, w8, w9, w24
	mov	w2, #1
	udiv	w26, w8, w10
	bl	FlashEraseBlocks
	adrp	x8, p_erase_count_table
	ldr	x8, [x8, :lo12:p_erase_count_table]
	lsl	x9, x26, #1
	adrp	x11, g_totle_sys_slc_erase_count
	adrp	x12, gSysFreeQueue
	ldrh	w10, [x8, x9]
	add	x12, x12, :lo12:gSysFreeQueue
	add	w10, w10, #1                    // =1
	strh	w10, [x8, x9]
	ldr	w8, [x11, :lo12:g_totle_sys_slc_erase_count]
	ldrh	w9, [x12, #6]
	ldrh	w10, [x12, #4]
	add	w8, w8, #1                      // =1
	add	w9, w9, #1                      // =1
	str	w8, [x11, :lo12:g_totle_sys_slc_erase_count]
	add	x8, x12, x10, lsl #1
	add	w10, w10, #1                    // =1
	strh	w9, [x12, #6]
	strh	w24, [x8, #8]
	and	w8, w10, #0x3ff
	strh	w8, [x12, #4]
	ldr	w8, [x25, #4]
.LBB96_25:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x9, gSysInfo+8
	ldr	w9, [x9, :lo12:gSysInfo+8]
	cmp	w9, w8
	b.hs	.LBB96_79
// %bb.26:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x10, gSysInfo
	add	x10, x10, :lo12:gSysInfo
	ldrh	w8, [x10]
	lsr	w9, w27, #10
	strh	w9, [x10]
	strh	w8, [x10, #4]
	ldr	w8, [x25, #4]
	b	.LBB96_78
.LBB96_27:                              //   in Loop: Header=BB96_12 Depth=2
	mov	w8, #65535
	cmp	w9, w8
	b.eq	.LBB96_73
// %bb.28:                              //   in Loop: Header=BB96_12 Depth=2
	mov	w8, #61634
	cmp	w9, w8
	b.ne	.LBB96_11
// %bb.29:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x24, g_totle_map_block
	adrp	x26, c_ftl_nand_max_map_blks
	ldrh	w9, [x24, :lo12:g_totle_map_block]
	ldr	w11, [x26, :lo12:c_ftl_nand_max_map_blks]
	cmp	w11, w9
	b.hs	.LBB96_31
// %bb.30:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlScanSysBlk
	mov	w2, #1232
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlScanSysBlk
	bl	sftl_printk
	ldr	w11, [x26, :lo12:c_ftl_nand_max_map_blks]
	ldrh	w9, [x24, :lo12:g_totle_map_block]
.LBB96_31:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x8, p_map_block_ver_table
	ldr	x10, [x8, :lo12:p_map_block_ver_table]
	mvn	w8, w9
	add	w8, w11, w8
	sxth	w13, w8
	mov	w14, w11
.LBB96_32:                              //   Parent Loop BB96_3 Depth=1
                                        //     Parent Loop BB96_12 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	sub	w14, w14, #1                    // =1
	sxth	w8, w14
	cmp	w13, w8
	b.ge	.LBB96_43
// %bb.33:                              //   in Loop: Header=BB96_32 Depth=3
	ldr	w12, [x25, #4]
	ldr	w15, [x10, w8, sxtw #2]
	cmp	w12, w15
	b.ls	.LBB96_32
// %bb.34:                              //   in Loop: Header=BB96_12 Depth=2
	cmp	w11, w9
	b.eq	.LBB96_37
// %bb.35:                              //   in Loop: Header=BB96_12 Depth=2
	ldr	w11, [x10]
	cbnz	w11, .LBB96_37
// %bb.36:                              //   in Loop: Header=BB96_12 Depth=2
	add	w9, w9, #1                      // =1
	adrp	x11, g_totle_map_block
	strh	w9, [x11, :lo12:g_totle_map_block]
.LBB96_37:                              //   in Loop: Header=BB96_12 Depth=2
	cbz	w8, .LBB96_42
// %bb.38:                              //   in Loop: Header=BB96_12 Depth=2
	ldr	w9, [x10, #4]
	adrp	x15, p_map_block_table
	cmp	w8, #1                          // =1
	adrp	x14, p_map_block_ver_table
	str	w9, [x10]
	ldr	x9, [x15, :lo12:p_map_block_table]
	ldrh	w10, [x9, #2]
	strh	w10, [x9]
	b.eq	.LBB96_41
// %bb.39:                              //   in Loop: Header=BB96_12 Depth=2
	mov	w9, #1
.LBB96_40:                              //   Parent Loop BB96_3 Depth=1
                                        //     Parent Loop BB96_12 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	ldr	x10, [x14, :lo12:p_map_block_ver_table]
	sxth	x11, w9
	add	x12, x11, #1                    // =1
	add	w9, w9, #1                      // =1
	ldr	w13, [x10, x12, lsl #2]
	str	w13, [x10, x11, lsl #2]
	ldr	x10, [x15, :lo12:p_map_block_table]
	and	w13, w9, #0xffff
	cmp	w13, w8, uxth
	ldrh	w12, [x10, x12, lsl #1]
	strh	w12, [x10, x11, lsl #1]
	b.lo	.LBB96_40
.LBB96_41:                              //   in Loop: Header=BB96_12 Depth=2
	ldr	w12, [x25, #4]
	ldr	x10, [x14, :lo12:p_map_block_ver_table]
.LBB96_42:                              //   in Loop: Header=BB96_12 Depth=2
	sxtw	x9, w8
	str	w12, [x10, x9, lsl #2]
	adrp	x10, p_map_block_table
	ldr	x10, [x10, :lo12:p_map_block_table]
	lsr	w11, w27, #10
	strh	w11, [x10, x9, lsl #1]
.LBB96_43:                              //   in Loop: Header=BB96_12 Depth=2
	tbnz	w8, #31, .LBB96_11
// %bb.44:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x9, g_totle_map_block
	ldrh	w9, [x9, :lo12:g_totle_map_block]
	adrp	x10, c_ftl_nand_max_map_blks
	ldr	w10, [x10, :lo12:c_ftl_nand_max_map_blks]
	mvn	w11, w9
	add	w10, w10, w11
	cmp	w8, w10, sxth
	b.gt	.LBB96_11
// %bb.45:                              //   in Loop: Header=BB96_12 Depth=2
	add	w9, w9, #1                      // =1
	adrp	x10, g_totle_map_block
	strh	w9, [x10, :lo12:g_totle_map_block]
	adrp	x10, p_map_block_ver_table
	ldr	w9, [x25, #4]
	ldr	x10, [x10, :lo12:p_map_block_ver_table]
                                        // kill: def $w8 killed $w8 killed $x8 def $x8
	and	x8, x8, #0xffff
	str	w9, [x10, x8, lsl #2]
	adrp	x9, p_map_block_table
	ldr	x9, [x9, :lo12:p_map_block_table]
	b	.LBB96_10
.LBB96_46:                              //   in Loop: Header=BB96_12 Depth=2
	lsl	x24, x23, #5
	add	x8, x8, x24
	add	w9, w27, #1                     // =1
	str	w9, [x8, #4]
	ldr	x8, [x22, :lo12:req_read]
	mov	w1, #1
	add	x0, x8, x24
	bl	FlashReadPages
	ldrsh	w8, [x25]
	cmn	w8, #1                          // =1
	b.eq	.LBB96_51
// %bb.47:                              //   in Loop: Header=BB96_12 Depth=2
	mov	w26, wzr
.LBB96_48:                              //   Parent Loop BB96_3 Depth=1
                                        //     Parent Loop BB96_12 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	ldr	x8, [x22, :lo12:req_read]
	ldr	w9, [x8, x24]
	cmn	w9, #1                          // =1
	b.ne	.LBB96_13
// %bb.49:                              //   in Loop: Header=BB96_48 Depth=3
	and	w9, w26, #0xffff
	cmp	w9, #14                         // =14
	b.hi	.LBB96_52
// %bb.50:                              //   in Loop: Header=BB96_48 Depth=3
	add	x8, x8, x24
	ldr	w9, [x8, #4]
	mov	w1, #1
	add	w26, w26, #1                    // =1
	add	w9, w9, #1                      // =1
	str	w9, [x8, #4]
	ldr	x8, [x22, :lo12:req_read]
	add	x0, x8, x24
	bl	FlashReadPages
	ldrsh	w8, [x25]
	cmn	w8, #1                          // =1
	b.ne	.LBB96_48
.LBB96_51:                              //   in Loop: Header=BB96_12 Depth=2
	ldr	x8, [x22, :lo12:req_read]
	mov	w9, #-1
	str	w9, [x8, x24]
	ldr	x8, [x22, :lo12:req_read]
	ldr	w8, [x8, x24]
	cmn	w8, #1                          // =1
	b.ne	.LBB96_13
.LBB96_52:                              //   in Loop: Header=BB96_12 Depth=2
	ubfx	w8, w27, #10, #16
	cbz	w8, .LBB96_11
// %bb.53:                              //   in Loop: Header=BB96_12 Depth=2
	mov	w9, #65535
	cmp	w8, w9
	b.eq	.LBB96_11
// %bb.54:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x9, gSysFreeQueue+6
	ldrh	w9, [x9, :lo12:gSysFreeQueue+6]
	cmp	w9, #1024                       // =1024
	b.eq	.LBB96_11
// %bb.55:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x13, req_erase
	ldr	x9, [x13, :lo12:req_erase]
	ldrh	w10, [x20, :lo12:c_ftl_nand_blks_per_die]
	and	w12, w27, #0x3fffc00
	ldrh	w11, [x21, :lo12:c_ftl_nand_planes_per_die]
	str	w12, [x9, #4]
	ldr	x0, [x13, :lo12:req_erase]
	udiv	w9, w8, w10
	msub	w8, w9, w10, w8
	mov	w2, #1
	lsr	w24, w27, #10
	udiv	w25, w8, w11
	bl	FlashEraseBlocks
	adrp	x8, p_erase_count_table
	ldr	x8, [x8, :lo12:p_erase_count_table]
	lsl	x9, x25, #1
	adrp	x11, g_totle_sys_slc_erase_count
	adrp	x12, gSysFreeQueue
	ldrh	w10, [x8, x9]
	add	x12, x12, :lo12:gSysFreeQueue
	add	w10, w10, #1                    // =1
	strh	w10, [x8, x9]
	ldr	w8, [x11, :lo12:g_totle_sys_slc_erase_count]
	ldrh	w9, [x12, #6]
	ldrh	w10, [x12, #4]
	add	w8, w8, #1                      // =1
	add	w9, w9, #1                      // =1
	str	w8, [x11, :lo12:g_totle_sys_slc_erase_count]
	add	x8, x12, x10, lsl #1
	add	w10, w10, #1                    // =1
	strh	w9, [x12, #6]
	strh	w24, [x8, #8]
	and	w8, w10, #0x3ff
	strh	w8, [x12, #4]
	b	.LBB96_11
.LBB96_56:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x8, g_totle_vendor_block
	adrp	x24, c_ftl_nand_max_vendor_blks
	ldrh	w10, [x8, :lo12:g_totle_vendor_block]
	ldrh	w11, [x24, :lo12:c_ftl_nand_max_vendor_blks]
	cmp	w10, w11
	b.ls	.LBB96_58
// %bb.57:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlScanSysBlk
	mov	w2, #1273
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlScanSysBlk
	mov	x26, x8
	bl	sftl_printk
	ldrh	w11, [x24, :lo12:c_ftl_nand_max_vendor_blks]
	ldrh	w10, [x26, :lo12:g_totle_vendor_block]
.LBB96_58:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x8, p_vendor_block_ver_table
	ldr	x9, [x8, :lo12:p_vendor_block_ver_table]
	mvn	w8, w10
	add	w13, w8, w11
	mov	w8, w11
.LBB96_59:                              //   Parent Loop BB96_3 Depth=1
                                        //     Parent Loop BB96_12 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	sub	w8, w8, #1                      // =1
	sxth	w8, w8
	cmp	w13, w8
	b.ge	.LBB96_70
// %bb.60:                              //   in Loop: Header=BB96_59 Depth=3
	ldr	w12, [x25, #4]
	ldr	w14, [x9, w8, sxtw #2]
	cmp	w12, w14
	b.ls	.LBB96_59
// %bb.61:                              //   in Loop: Header=BB96_12 Depth=2
	cmp	w10, w11
	b.eq	.LBB96_64
// %bb.62:                              //   in Loop: Header=BB96_12 Depth=2
	ldr	w11, [x9]
	cbnz	w11, .LBB96_64
// %bb.63:                              //   in Loop: Header=BB96_12 Depth=2
	add	w10, w10, #1                    // =1
	adrp	x11, g_totle_vendor_block
	strh	w10, [x11, :lo12:g_totle_vendor_block]
.LBB96_64:                              //   in Loop: Header=BB96_12 Depth=2
	cbz	w8, .LBB96_69
// %bb.65:                              //   in Loop: Header=BB96_12 Depth=2
	ldr	w10, [x9, #4]
	adrp	x15, p_vendor_block_table
	cmp	w8, #1                          // =1
	adrp	x14, p_vendor_block_ver_table
	str	w10, [x9]
	ldr	x9, [x15, :lo12:p_vendor_block_table]
	ldrh	w10, [x9, #2]
	strh	w10, [x9]
	b.eq	.LBB96_68
// %bb.66:                              //   in Loop: Header=BB96_12 Depth=2
	mov	w9, #1
.LBB96_67:                              //   Parent Loop BB96_3 Depth=1
                                        //     Parent Loop BB96_12 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	ldr	x10, [x14, :lo12:p_vendor_block_ver_table]
	sxth	x11, w9
	add	x12, x11, #1                    // =1
	add	w9, w9, #1                      // =1
	ldr	w13, [x10, x12, lsl #2]
	str	w13, [x10, x11, lsl #2]
	ldr	x10, [x15, :lo12:p_vendor_block_table]
	and	w13, w9, #0xffff
	cmp	w13, w8, uxth
	ldrh	w12, [x10, x12, lsl #1]
	strh	w12, [x10, x11, lsl #1]
	b.lo	.LBB96_67
.LBB96_68:                              //   in Loop: Header=BB96_12 Depth=2
	ldr	w12, [x25, #4]
	ldr	x9, [x14, :lo12:p_vendor_block_ver_table]
.LBB96_69:                              //   in Loop: Header=BB96_12 Depth=2
	sxtw	x10, w8
	str	w12, [x9, x10, lsl #2]
	adrp	x9, p_vendor_block_table
	ldr	x9, [x9, :lo12:p_vendor_block_table]
	lsr	w11, w27, #10
	strh	w11, [x9, x10, lsl #1]
.LBB96_70:                              //   in Loop: Header=BB96_12 Depth=2
	tbnz	w8, #31, .LBB96_11
// %bb.71:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x9, g_totle_vendor_block
	ldrh	w9, [x9, :lo12:g_totle_vendor_block]
	adrp	x10, c_ftl_nand_max_vendor_blks
	ldrh	w10, [x10, :lo12:c_ftl_nand_max_vendor_blks]
	mvn	w11, w9
	add	w10, w10, w11
	cmp	w8, w10, sxth
	b.gt	.LBB96_11
// %bb.72:                              //   in Loop: Header=BB96_12 Depth=2
	add	w9, w9, #1                      // =1
	adrp	x10, g_totle_vendor_block
	strh	w9, [x10, :lo12:g_totle_vendor_block]
	adrp	x10, p_vendor_block_ver_table
	ldr	w9, [x25, #4]
	ldr	x10, [x10, :lo12:p_vendor_block_ver_table]
                                        // kill: def $w8 killed $w8 killed $x8 def $x8
	and	x8, x8, #0xffff
	str	w9, [x10, x8, lsl #2]
	adrp	x9, p_vendor_block_table
	ldr	x9, [x9, :lo12:p_vendor_block_table]
	b	.LBB96_10
.LBB96_73:                              //   in Loop: Header=BB96_12 Depth=2
	ubfx	w8, w27, #10, #16
	cbz	w8, .LBB96_11
// %bb.74:                              //   in Loop: Header=BB96_12 Depth=2
	mov	w9, #65535
	cmp	w8, w9
	b.eq	.LBB96_11
// %bb.75:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x8, gSysFreeQueue+6
	ldrh	w8, [x8, :lo12:gSysFreeQueue+6]
	cmp	w8, #1024                       // =1024
	b.eq	.LBB96_11
// %bb.76:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x11, gSysFreeQueue
	add	x11, x11, :lo12:gSysFreeQueue
	ldrh	w9, [x11, #4]
	add	w8, w8, #1                      // =1
	lsr	w10, w27, #10
	strh	w8, [x11, #6]
	add	x8, x11, x9, lsl #1
	add	w9, w9, #1                      // =1
	strh	w10, [x8, #8]
	and	w8, w9, #0x3ff
	strh	w8, [x11, #4]
	b	.LBB96_11
.LBB96_77:                              //   in Loop: Header=BB96_12 Depth=2
	adrp	x10, gSysInfo
	lsr	w9, w27, #10
	add	x10, x10, :lo12:gSysInfo
	strh	w9, [x10]
.LBB96_78:                              //   in Loop: Header=BB96_12 Depth=2
	str	w8, [x10, #8]
	b	.LBB96_11
.LBB96_79:                              //   in Loop: Header=BB96_12 Depth=2
	lsr	w8, w27, #10
	adrp	x9, gSysInfo+4
	strh	w8, [x9, :lo12:gSysInfo+4]
	b	.LBB96_11
.LBB96_80:
	adrp	x17, p_map_block_table
	ldr	x10, [x17, :lo12:p_map_block_table]
	adrp	x16, c_ftl_nand_max_map_blks
	ldr	w8, [x16, :lo12:c_ftl_nand_max_map_blks]
	adrp	x15, g_totle_map_block
	ldrh	w9, [x10]
	cbnz	w9, .LBB96_90
// %bb.81:
	ldrh	w9, [x15, :lo12:g_totle_map_block]
	adrp	x18, c_ftl_nand_max_vendor_blks
	adrp	x0, p_vendor_block_table
	cbz	w9, .LBB96_91
// %bb.82:
	cbz	w8, .LBB96_91
// %bb.83:
	mov	w13, wzr
	adrp	x14, p_map_block_ver_table
.LBB96_84:                              // =>This Inner Loop Header: Depth=1
	mov	w11, w13
	add	w13, w13, #1                    // =1
	sxth	w9, w13
	cmp	w8, w9
	b.ls	.LBB96_91
// %bb.85:                              //   in Loop: Header=BB96_84 Depth=1
	ldrh	w12, [x10, w9, sxtw #1]
	cbz	w12, .LBB96_84
// %bb.86:
	cmp	w8, w9
	b.ls	.LBB96_91
// %bb.87:
	sub	w8, w9, w9
	strh	w12, [x10, w8, sxtw #1]
	ldr	x10, [x14, :lo12:p_map_block_ver_table]
	sxtw	x12, w9
	ldr	w13, [x10, x12, lsl #2]
	str	w13, [x10, w8, sxtw #2]
	ldr	x8, [x17, :lo12:p_map_block_table]
	add	w10, w11, #2                    // =2
	strh	wzr, [x8, x12, lsl #1]
	ldr	w8, [x16, :lo12:c_ftl_nand_max_map_blks]
.LBB96_88:                              // =>This Inner Loop Header: Depth=1
	sxth	w10, w10
	cmp	w8, w10
	b.ls	.LBB96_91
// %bb.89:                              //   in Loop: Header=BB96_88 Depth=1
	ldr	x8, [x17, :lo12:p_map_block_table]
	sbfiz	x11, x10, #1, #16
	sub	w13, w10, w9
	ldrh	w12, [x8, x11]
	strh	w12, [x8, w13, sxtw #1]
	ldr	x8, [x14, :lo12:p_map_block_ver_table]
	sxth	x12, w10
	add	w10, w10, #1                    // =1
	ldr	w12, [x8, x12, lsl #2]
	str	w12, [x8, w13, sxtw #2]
	ldr	x8, [x17, :lo12:p_map_block_table]
	strh	wzr, [x8, x11]
	ldr	w8, [x16, :lo12:c_ftl_nand_max_map_blks]
	b	.LBB96_88
.LBB96_90:
	adrp	x18, c_ftl_nand_max_vendor_blks
	adrp	x0, p_vendor_block_table
.LBB96_91:
	ldr	x10, [x0, :lo12:p_vendor_block_table]
	ldrh	w9, [x10]
	cbnz	w9, .LBB96_102
// %bb.92:
	adrp	x9, g_totle_vendor_block
	ldrh	w9, [x9, :lo12:g_totle_vendor_block]
	cbz	w9, .LBB96_102
// %bb.93:
	ldrh	w12, [x18, :lo12:c_ftl_nand_max_vendor_blks]
	cbz	w12, .LBB96_102
// %bb.94:
	mov	w14, wzr
	adrp	x17, p_vendor_block_ver_table
.LBB96_95:                              // =>This Inner Loop Header: Depth=1
	mov	w11, w14
	add	w14, w14, #1                    // =1
	sxth	w9, w14
	cmp	w9, w12
	b.ge	.LBB96_102
// %bb.96:                              //   in Loop: Header=BB96_95 Depth=1
	ldrh	w13, [x10, w9, sxtw #1]
	cbz	w13, .LBB96_95
// %bb.97:
	cmp	w9, w12
	b.ge	.LBB96_102
// %bb.98:
	sub	w8, w9, w9
	strh	w13, [x10, w8, sxtw #1]
	ldr	x10, [x17, :lo12:p_vendor_block_ver_table]
	sxtw	x12, w9
	ldr	w13, [x10, x12, lsl #2]
	str	w13, [x10, w8, sxtw #2]
	ldr	x8, [x0, :lo12:p_vendor_block_table]
	strh	wzr, [x8, x12, lsl #1]
	ldrh	w10, [x18, :lo12:c_ftl_nand_max_vendor_blks]
	add	w8, w11, #2                     // =2
.LBB96_99:                              // =>This Inner Loop Header: Depth=1
	sxth	w8, w8
	cmp	w8, w10
	b.ge	.LBB96_101
// %bb.100:                             //   in Loop: Header=BB96_99 Depth=1
	ldr	x10, [x0, :lo12:p_vendor_block_table]
	sbfiz	x11, x8, #1, #16
	sub	w13, w8, w9
	ldrh	w12, [x10, x11]
	strh	w12, [x10, w13, sxtw #1]
	ldr	x10, [x17, :lo12:p_vendor_block_ver_table]
	sxth	x12, w8
	add	w8, w8, #1                      // =1
	ldr	w12, [x10, x12, lsl #2]
	str	w12, [x10, w13, sxtw #2]
	ldr	x10, [x0, :lo12:p_vendor_block_table]
	strh	wzr, [x10, x11]
	ldrh	w10, [x18, :lo12:c_ftl_nand_max_vendor_blks]
	b	.LBB96_99
.LBB96_101:
	ldr	w8, [x16, :lo12:c_ftl_nand_max_map_blks]
.LBB96_102:
	ldrh	w9, [x15, :lo12:g_totle_map_block]
	cmp	w8, w9
	b.hs	.LBB96_104
// %bb.103:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlScanSysBlk
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlScanSysBlk
	mov	w2, #1398
	bl	sftl_printk
.LBB96_104:
	ldp	x20, x19, [sp, #80]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #64]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #48]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #32]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #96             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end96:
	.size	FtlScanSysBlk, .Lfunc_end96-FtlScanSysBlk
                                        // -- End function
	.globl	FtlLoadEctTbl                   // -- Begin function FtlLoadEctTbl
	.p2align	2
	.type	FtlLoadEctTbl,@function
FtlLoadEctTbl:                          // @FtlLoadEctTbl
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #16]             // 16-byte Folded Spill
	adrp	x19, g_ect_tbl_info_size
	adrp	x20, gp_ect_tbl_info
	ldrh	w1, [x19, :lo12:g_ect_tbl_info_size]
	ldr	x2, [x20, :lo12:gp_ect_tbl_info]
	mov	w0, #64
	mov	x29, sp
	bl	FtlVendorPartRead
	ldr	x8, [x20, :lo12:gp_ect_tbl_info]
	mov	w9, #17221
	movk	w9, #16980, lsl #16
	ldr	w8, [x8]
	cmp	w8, w9
	b.eq	.LBB97_2
// %bb.1:
	adrp	x0, .L.str.75
	adrp	x1, .L.str.96
	add	x0, x0, :lo12:.L.str.75
	add	x1, x1, :lo12:.L.str.96
	bl	sftl_printk
	ldrh	w8, [x19, :lo12:g_ect_tbl_info_size]
	ldr	x0, [x20, :lo12:gp_ect_tbl_info]
	mov	w1, wzr
	lsl	x2, x8, #9
	bl	memset
.LBB97_2:
	ldp	x20, x19, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end97:
	.size	FtlLoadEctTbl, .Lfunc_end97-FtlLoadEctTbl
                                        // -- End function
	.globl	FtlLoadSysInfo                  // -- Begin function FtlLoadSysInfo
	.p2align	2
	.type	FtlLoadSysInfo,@function
FtlLoadSysInfo:                         // @FtlLoadSysInfo
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-96]!           // 16-byte Folded Spill
	adrp	x11, c_ftl_nand_data_blks_per_plane
	stp	x28, x27, [sp, #16]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #32]             // 16-byte Folded Spill
	adrp	x25, p_sys_data_buf
	adrp	x27, p_sys_spare_buf
	adrp	x12, p_valid_page_count_table
	ldrh	w11, [x11, :lo12:c_ftl_nand_data_blks_per_plane]
	ldr	x9, [x25, :lo12:p_sys_data_buf]
	ldr	x10, [x27, :lo12:p_sys_spare_buf]
	ldr	x0, [x12, :lo12:p_valid_page_count_table]
	adrp	x8, req_sys+8
	add	x8, x8, :lo12:req_sys+8
	lsl	x2, x11, #1
	mov	w1, wzr
	stp	x24, x23, [sp, #48]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #64]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #80]             // 16-byte Folded Spill
	mov	x29, sp
	stp	x9, x10, [x8]
	bl	memset
	adrp	x20, gSysInfo
	ldrh	w0, [x20, :lo12:gSysInfo]
	mov	w8, #65535
	cmp	w0, w8
	b.eq	.LBB98_39
// %bb.1:
	mov	w26, #19539
	mov	w1, #1
	movk	w26, #18004, lsl #16
	bl	FtlGetLastWrittenPage
	add	w8, w0, #1                      // =1
	adrp	x9, gSysInfo+2
	adrp	x21, c_ftl_nand_byte_pre_page
	strh	w8, [x9, :lo12:gSysInfo+2]
	tbnz	w0, #15, .LBB98_19
// %bb.2:
	ldrh	w8, [x20, :lo12:gSysInfo]
	adrp	x20, req_sys
	mov	w19, w0
	sxth	w24, w0
	add	x20, x20, :lo12:req_sys
	adrp	x22, gSysInfo+4
.LBB98_3:                               // =>This Loop Header: Depth=1
                                        //     Child Loop BB98_7 Depth 2
                                        //       Child Loop BB98_11 Depth 3
	sxth	w23, w8
	sbfiz	w28, w8, #10, #16
	b	.LBB98_7
.LBB98_4:                               //   in Loop: Header=BB98_7 Depth=2
	ldrh	w4, [x22, :lo12:gSysInfo+4]
	adrp	x0, .L.str.97
	adrp	x1, .L__func__.FtlLoadSysInfo
	add	x0, x0, :lo12:.L.str.97
	add	x1, x1, :lo12:.L__func__.FtlLoadSysInfo
	mov	w2, w23
	mov	w3, w24
	bl	sftl_printk
	tst	w19, #0xffff
	b.eq	.LBB98_16
.LBB98_5:                               //   in Loop: Header=BB98_7 Depth=2
	adrp	x8, req_sys
	mov	w9, #-1
	str	w9, [x8, :lo12:req_sys]
.LBB98_6:                               //   in Loop: Header=BB98_7 Depth=2
	sub	w8, w19, #1                     // =1
	sxth	w24, w8
	mov	w19, w24
	tbnz	w8, #15, .LBB98_19
.LBB98_7:                               //   Parent Loop BB98_3 Depth=1
                                        // =>  This Loop Header: Depth=2
                                        //       Child Loop BB98_11 Depth 3
	ldr	x8, [x25, :lo12:p_sys_data_buf]
	orr	w9, w28, w24
	mov	w1, #1
	mov	x0, x20
	str	w9, [x20, #4]
	str	x8, [x20, #8]
	bl	FlashReadPages
	ldr	x8, [x20, #16]
	ldr	w5, [x8, #12]
	ldr	w8, [x20]
	cbz	w5, .LBB98_13
// %bb.8:                               //   in Loop: Header=BB98_7 Depth=2
	cmn	w8, #1                          // =1
	b.eq	.LBB98_13
// %bb.9:                               //   in Loop: Header=BB98_7 Depth=2
	ldrh	w9, [x21, :lo12:c_ftl_nand_byte_pre_page]
	mov	w6, #42982
	movk	w6, #18374, lsl #16
	cbz	x9, .LBB98_12
// %bb.10:                              //   in Loop: Header=BB98_7 Depth=2
	adrp	x10, req_sys+8
	ldr	x10, [x10, :lo12:req_sys+8]
	mov	w6, #42982
	movk	w6, #18374, lsl #16
.LBB98_11:                              //   Parent Loop BB98_3 Depth=1
                                        //     Parent Loop BB98_7 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	ldrb	w11, [x10], #1
	lsr	w12, w6, #2
	add	w12, w12, w6, lsl #5
	subs	x9, x9, #1                      // =1
	add	w11, w12, w11
	eor	w6, w11, w6
	b.ne	.LBB98_11
.LBB98_12:                              //   in Loop: Header=BB98_7 Depth=2
	cmp	w5, w6
	b.ne	.LBB98_4
.LBB98_13:                              //   in Loop: Header=BB98_7 Depth=2
	cmn	w8, #1                          // =1
	b.eq	.LBB98_6
// %bb.14:                              //   in Loop: Header=BB98_7 Depth=2
	ldr	x8, [x25, :lo12:p_sys_data_buf]
	ldr	w8, [x8]
	cmp	w8, w26
	b.ne	.LBB98_6
// %bb.15:                              //   in Loop: Header=BB98_7 Depth=2
	ldr	x8, [x27, :lo12:p_sys_spare_buf]
	ldrsh	w8, [x8]
	cmn	w8, #3932                       // =3932
	b.ne	.LBB98_6
	b	.LBB98_18
.LBB98_16:                              //   in Loop: Header=BB98_7 Depth=2
	ldrh	w8, [x22, :lo12:gSysInfo+4]
	cmp	w23, w8
	b.eq	.LBB98_5
// %bb.17:                              //   in Loop: Header=BB98_3 Depth=1
	adrp	x9, c_ftl_nand_page_pre_slc_blk
	ldrh	w9, [x9, :lo12:c_ftl_nand_page_pre_slc_blk]
	sub	w9, w9, #1                      // =1
	sxth	w24, w9
	mov	w19, w24
	tbz	w9, #15, .LBB98_3
	b	.LBB98_19
.LBB98_18:
	tbz	w19, #15, .LBB98_20
.LBB98_19:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlLoadSysInfo
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlLoadSysInfo
	mov	w2, #1474
	bl	sftl_printk
.LBB98_20:
	adrp	x19, c_ftl_nand_data_blks_per_plane
	ldrh	w8, [x19, :lo12:c_ftl_nand_data_blks_per_plane]
	ldrh	w9, [x21, :lo12:c_ftl_nand_byte_pre_page]
	lsl	x2, x8, #1
	add	x8, x2, #48                     // =48
	cmp	x8, x9
	b.ls	.LBB98_22
// %bb.21:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlLoadSysInfo
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlLoadSysInfo
	mov	w2, #1476
	bl	sftl_printk
	ldrh	w8, [x19, :lo12:c_ftl_nand_data_blks_per_plane]
	lsl	x2, x8, #1
.LBB98_22:
	adrp	x8, req_sys+8
	ldr	x8, [x8, :lo12:req_sys+8]
	adrp	x19, g_sys_save_data
	add	x19, x19, :lo12:g_sys_save_data
	adrp	x13, p_valid_page_count_table
	ldp	x9, x10, [x8]
	ldr	x0, [x13, :lo12:p_valid_page_count_table]
	add	x1, x8, #48                     // =48
	stp	x9, x10, [x19]
	ldp	x9, x10, [x8, #32]
	ldp	x12, x11, [x8, #16]
	stp	x9, x10, [x19, #32]
	stp	x12, x11, [x19, #16]
	bl	memcpy
	ldr	w8, [x19]
	cmp	w8, w26
	b.ne	.LBB98_39
// %bb.23:
	adrp	x8, g_sys_save_data+8
	add	x8, x8, :lo12:g_sys_save_data+8
	adrp	x9, c_ftl_nand_die_num
	ldrb	w10, [x8, #2]
	ldrh	w9, [x9, :lo12:c_ftl_nand_die_num]
	ldrh	w8, [x8]
	adrp	x11, gSysInfo+6
	cmp	w9, w10
	strh	w8, [x11, :lo12:gSysInfo+6]
	b.ne	.LBB98_39
// %bb.24:
	adrp	x9, c_ftl_nand_max_data_blks
	adrp	x10, gBbtInfo+6
	adrp	x12, c_ftl_nand_page_pre_blk
	ldr	w9, [x9, :lo12:c_ftl_nand_max_data_blks]
	ldrh	w10, [x10, :lo12:gBbtInfo+6]
	ldrh	w12, [x12, :lo12:c_ftl_nand_page_pre_blk]
	adrp	x11, g_MaxLbn
	str	w8, [x11, :lo12:g_MaxLbn]
	subs	w11, w9, w8
	cmp	w9, w8
	mul	w8, w12, w8
	adrp	x9, g_MaxLpn
	sub	w10, w11, w10
	adrp	x11, c_ftl_nand_sec_pre_page
	ldrh	w11, [x11, :lo12:c_ftl_nand_sec_pre_page]
	str	w8, [x9, :lo12:g_MaxLpn]
	adrp	x9, c_ftl_nand_planes_num
	ldrh	w9, [x9, :lo12:c_ftl_nand_planes_num]
	mul	w8, w8, w11
	adrp	x11, g_MaxLbaSector
	str	w8, [x11, :lo12:g_MaxLbaSector]
	udiv	w8, w10, w9
	adrp	x9, c_ftl_nand_data_op_blks_per_plane
	strh	w8, [x9, :lo12:c_ftl_nand_data_op_blks_per_plane]
	b.hs	.LBB98_26
// %bb.25:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlLoadSysInfo
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlLoadSysInfo
	mov	w2, #1498
	bl	sftl_printk
.LBB98_26:
	adrp	x8, g_sys_save_data+11
	add	x8, x8, :lo12:g_sys_save_data+11
	ldur	w11, [x8, #21]
	adrp	x13, g_totle_mlc_erase_count
	ldurh	w14, [x8, #11]
	adrp	x12, g_gc_temp_superblock
	str	w11, [x13, :lo12:g_totle_mlc_erase_count]
	ldrb	w11, [x8, #2]
	ldurh	w13, [x8, #13]
	adrp	x10, g_gc_superblock
	add	x12, x12, :lo12:g_gc_temp_superblock
	add	x10, x10, :lo12:g_gc_superblock
	strh	w14, [x12]
	mov	w14, #65535
	strb	w11, [x12, #8]
	lsr	w11, w13, #6
	strh	w11, [x12, #2]
	and	w11, w13, #0x3f
	str	w14, [x10]
	adrp	x13, g_totle_read_page_count
	adrp	x14, g_totle_slc_erase_count
	str	wzr, [x13, :lo12:g_totle_read_page_count]
	ldrb	w13, [x8]
	str	wzr, [x14, :lo12:g_totle_slc_erase_count]
	ldurh	w14, [x8, #5]
	adrp	x9, g_active_superblock
	strb	w11, [x12, #6]
	adrp	x12, g_totle_write_page_count
	add	x9, x9, :lo12:g_active_superblock
	str	wzr, [x12, :lo12:g_totle_write_page_count]
	adrp	x12, g_max_erase_count
	str	wzr, [x12, :lo12:g_max_erase_count]
	strb	w13, [x9, #8]
	lsr	w12, w14, #6
	and	w13, w14, #0x3f
	strb	wzr, [x10, #6]
	strb	wzr, [x10, #8]
	adrp	x10, g_totle_l2p_write_count
	strh	w12, [x9, #2]
	ldurh	w12, [x8, #9]
	strb	w13, [x9, #6]
	ldrb	w13, [x8, #1]
	str	wzr, [x10, :lo12:g_totle_l2p_write_count]
	ldurh	w10, [x8, #3]
	adrp	x15, g_buffer_superblock
	add	x15, x15, :lo12:g_buffer_superblock
	adrp	x11, g_totle_gc_page_count
	str	wzr, [x11, :lo12:g_totle_gc_page_count]
	adrp	x11, g_GlobalSysVersion
	strb	w13, [x15, #8]
	lsr	w13, w12, #6
	strh	w10, [x9]
	ldurh	w9, [x8, #7]
	ldur	w8, [x8, #29]
	strh	w13, [x15, #2]
	ldr	w13, [x11, :lo12:g_GlobalSysVersion]
	and	w12, w12, #0x3f
	strb	w12, [x15, #6]
	adrp	x12, g_totle_cache_write_count
	cmp	w8, w13
	strh	w9, [x15]
	str	wzr, [x12, :lo12:g_totle_cache_write_count]
	b.ls	.LBB98_28
// %bb.27:
	str	w8, [x11, :lo12:g_GlobalSysVersion]
.LBB98_28:
	adrp	x11, g_sys_save_data+36
	adrp	x8, g_GlobalDataVersion
	ldr	w11, [x11, :lo12:g_sys_save_data+36]
	ldr	w12, [x8, :lo12:g_GlobalDataVersion]
	cmp	w11, w12
	b.ls	.LBB98_30
// %bb.29:
	str	w11, [x8, :lo12:g_GlobalDataVersion]
.LBB98_30:
	mov	w19, #65535
	cmp	w10, w19
	b.eq	.LBB98_32
// %bb.31:
	adrp	x0, g_active_superblock
	add	x0, x0, :lo12:g_active_superblock
	bl	make_superblock
	adrp	x8, g_buffer_superblock
	ldrh	w9, [x8, :lo12:g_buffer_superblock]
.LBB98_32:
	cmp	w9, w19
	b.eq	.LBB98_34
// %bb.33:
	adrp	x0, g_buffer_superblock
	add	x0, x0, :lo12:g_buffer_superblock
	bl	make_superblock
.LBB98_34:
	adrp	x8, g_gc_temp_superblock
	ldrsh	w8, [x8, :lo12:g_gc_temp_superblock]
	cmn	w8, #1                          // =1
	b.eq	.LBB98_36
// %bb.35:
	adrp	x0, g_gc_temp_superblock
	add	x0, x0, :lo12:g_gc_temp_superblock
	bl	make_superblock
.LBB98_36:
	adrp	x8, g_gc_superblock
	ldrsh	w8, [x8, :lo12:g_gc_superblock]
	cmn	w8, #1                          // =1
	b.eq	.LBB98_38
// %bb.37:
	adrp	x0, g_gc_superblock
	add	x0, x0, :lo12:g_gc_superblock
	bl	make_superblock
.LBB98_38:
	mov	w0, wzr
	b	.LBB98_40
.LBB98_39:
	mov	w0, #-1
.LBB98_40:
	ldp	x20, x19, [sp, #80]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #64]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #48]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #32]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #16]             // 16-byte Folded Reload
	ldp	x29, x30, [sp], #96             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end98:
	.size	FtlLoadSysInfo, .Lfunc_end98-FtlLoadSysInfo
                                        // -- End function
	.globl	FtlMapTblRecovery               // -- Begin function FtlMapTblRecovery
	.p2align	2
	.type	FtlMapTblRecovery,@function
FtlMapTblRecovery:                      // @FtlMapTblRecovery
// %bb.0:
	hint	#25
	sub	sp, sp, #128                    // =128
	stp	x20, x19, [sp, #112]            // 16-byte Folded Spill
	mov	x19, x0
	stp	x28, x27, [sp, #48]             // 16-byte Folded Spill
	ldrh	w28, [x19, #6]
	ldr	x0, [x0, #40]
	ldp	x27, x8, [x19, #16]
	stp	x22, x21, [sp, #96]             // 16-byte Folded Spill
	ldrh	w22, [x19, #8]
	stp	x29, x30, [sp, #32]             // 16-byte Folded Spill
	add	x29, sp, #32                    // =32
	lsl	x2, x28, #2
	mov	w1, wzr
	stp	x26, x25, [sp, #64]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #80]             // 16-byte Folded Spill
	str	x8, [sp, #8]                    // 8-byte Folded Spill
	stur	x0, [x29, #-8]                  // 8-byte Folded Spill
	bl	memset
	adrp	x10, p_sys_data_buf
	adrp	x9, p_sys_spare_buf
	ldr	x10, [x10, :lo12:p_sys_data_buf]
	ldr	x21, [x9, :lo12:p_sys_spare_buf]
	adrp	x8, req_sys+8
	add	x8, x8, :lo12:req_sys+8
	mov	w11, #-1
	mov	w9, #1
	adrp	x20, c_ftl_nand_page_pre_slc_blk
	stp	x10, x21, [x8]
	str	w11, [x19]
	str	xzr, [x19, #48]
	str	w9, [x19, #56]
	str	w22, [sp, #16]                  // 4-byte Folded Spill
	cbz	w22, .LBB99_40
// %bb.1:
	ldr	w8, [sp, #16]                   // 4-byte Folded Reload
	adrp	x22, req_sys
	mov	w23, wzr
	add	x22, x22, :lo12:req_sys
	sub	w8, w8, #1                      // =1
	adrp	x24, c_ftl_nand_byte_pre_page
	stur	w8, [x29, #-12]                 // 4-byte Folded Spill
	b	.LBB99_3
.LBB99_2:                               //   in Loop: Header=BB99_3 Depth=1
	add	w8, w23, #1                     // =1
	sxth	w23, w8
	ldr	w8, [sp, #16]                   // 4-byte Folded Reload
	cmp	w23, w8
	b.ge	.LBB99_40
.LBB99_3:                               // =>This Loop Header: Depth=1
                                        //     Child Loop BB99_10 Depth 2
                                        //     Child Loop BB99_16 Depth 2
                                        //       Child Loop BB99_20 Depth 3
	ldur	w8, [x29, #-12]                 // 4-byte Folded Reload
	cmp	w8, w23
	b.eq	.LBB99_26
// %bb.4:                               //   in Loop: Header=BB99_3 Depth=1
	adrp	x8, p_sys_data_buf
	ldr	x8, [x8, :lo12:p_sys_data_buf]
	sxth	x25, w23
	mov	w1, #1
	mov	x0, x22
	str	x8, [x22, #8]
	ldrh	w8, [x20, :lo12:c_ftl_nand_page_pre_slc_blk]
	ldrh	w9, [x27, x25, lsl #1]
	sub	w8, w8, #1                      // =1
	orr	w8, w8, w9, lsl #10
	str	w8, [x22, #4]
	bl	FlashReadPages
	ldr	w8, [x22]
	cmn	w8, #1                          // =1
	b.eq	.LBB99_12
// %bb.5:                               //   in Loop: Header=BB99_3 Depth=1
	ldrh	w8, [x21]
	ldrh	w9, [x19, #4]
	cmp	w8, w9
	b.ne	.LBB99_12
// %bb.6:                               //   in Loop: Header=BB99_3 Depth=1
	ldrsh	w8, [x21, #8]
	cmn	w8, #1291                       // =1291
	b.ne	.LBB99_12
// %bb.7:                               //   in Loop: Header=BB99_3 Depth=1
	ldrh	w8, [x20, :lo12:c_ftl_nand_page_pre_slc_blk]
	cmp	w8, #2                          // =2
	b.lo	.LBB99_2
// %bb.8:                               //   in Loop: Header=BB99_3 Depth=1
	mov	w9, wzr
	b	.LBB99_10
.LBB99_9:                               //   in Loop: Header=BB99_10 Depth=2
	add	w9, w9, #1                      // =1
	and	w10, w8, #0xffff
	sxth	w9, w9
	sub	w10, w10, #1                    // =1
	cmp	w10, w9
	b.le	.LBB99_2
.LBB99_10:                              //   Parent Loop BB99_3 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	adrp	x10, p_sys_data_buf
	ldr	x10, [x10, :lo12:p_sys_data_buf]
	sbfiz	x12, x9, #1, #32
	sxtw	x11, w12
	lsl	x11, x11, #2
	ldrh	w11, [x10, x11]
	cmp	w11, w28
	b.hs	.LBB99_9
// %bb.11:                              //   in Loop: Header=BB99_10 Depth=2
	lsl	x8, x12, #2
	orr	x8, x8, #0x4
	ldr	w8, [x10, x8]
	ldur	x10, [x29, #-8]                 // 8-byte Folded Reload
	str	w8, [x10, x11, lsl #2]
	ldrh	w8, [x20, :lo12:c_ftl_nand_page_pre_slc_blk]
	b	.LBB99_9
.LBB99_12:                              //   in Loop: Header=BB99_3 Depth=1
	ldrh	w8, [x20, :lo12:c_ftl_nand_page_pre_slc_blk]
	cbz	w8, .LBB99_2
// %bb.13:                              //   in Loop: Header=BB99_3 Depth=1
	mov	w26, wzr
	b	.LBB99_16
.LBB99_14:                              //   in Loop: Header=BB99_16 Depth=2
	adrp	x0, .L.str.99
	adrp	x1, .L__func__.FtlMapTblRecovery
	add	x0, x0, :lo12:.L.str.99
	add	x1, x1, :lo12:.L__func__.FtlMapTblRecovery
	mov	w2, w23
	mov	w3, w26
	bl	sftl_printk
	adrp	x8, req_sys
	mov	w9, #-1
	str	w9, [x8, :lo12:req_sys]
.LBB99_15:                              //   in Loop: Header=BB99_16 Depth=2
	ldrh	w8, [x20, :lo12:c_ftl_nand_page_pre_slc_blk]
	add	w9, w26, #1                     // =1
	sxth	w26, w9
	cmp	w26, w8
	b.ge	.LBB99_2
.LBB99_16:                              //   Parent Loop BB99_3 Depth=1
                                        // =>  This Loop Header: Depth=2
                                        //       Child Loop BB99_20 Depth 3
	ldrh	w8, [x27, x25, lsl #1]
	mov	w1, #1
	mov	x0, x22
	orr	w8, w26, w8, lsl #10
	str	w8, [x22, #4]
	bl	FlashReadPages
	ldr	x8, [x22, #16]
	ldr	w4, [x8, #12]
	ldr	w8, [x22]
	cbz	w4, .LBB99_22
// %bb.17:                              //   in Loop: Header=BB99_16 Depth=2
	cmn	w8, #1                          // =1
	b.eq	.LBB99_22
// %bb.18:                              //   in Loop: Header=BB99_16 Depth=2
	ldrh	w9, [x24, :lo12:c_ftl_nand_byte_pre_page]
	mov	w5, #42982
	movk	w5, #18374, lsl #16
	cbz	x9, .LBB99_21
// %bb.19:                              //   in Loop: Header=BB99_16 Depth=2
	adrp	x10, req_sys+8
	ldr	x10, [x10, :lo12:req_sys+8]
	mov	w5, #42982
	movk	w5, #18374, lsl #16
.LBB99_20:                              //   Parent Loop BB99_3 Depth=1
                                        //     Parent Loop BB99_16 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	ldrb	w11, [x10], #1
	lsr	w12, w5, #2
	add	w12, w12, w5, lsl #5
	subs	x9, x9, #1                      // =1
	add	w11, w12, w11
	eor	w5, w11, w5
	b.ne	.LBB99_20
.LBB99_21:                              //   in Loop: Header=BB99_16 Depth=2
	cmp	w4, w5
	b.ne	.LBB99_14
.LBB99_22:                              //   in Loop: Header=BB99_16 Depth=2
	cmn	w8, #1                          // =1
	b.eq	.LBB99_15
// %bb.23:                              //   in Loop: Header=BB99_16 Depth=2
	ldrh	w8, [x21, #8]
	cmp	w8, w28
	b.hs	.LBB99_15
// %bb.24:                              //   in Loop: Header=BB99_16 Depth=2
	ldrh	w9, [x21]
	ldrh	w10, [x19, #4]
	cmp	w9, w10
	b.ne	.LBB99_15
// %bb.25:                              //   in Loop: Header=BB99_16 Depth=2
	adrp	x9, req_sys+4
	ldr	w9, [x9, :lo12:req_sys+4]
	ldur	x10, [x29, #-8]                 // 8-byte Folded Reload
	str	w9, [x10, x8, lsl #2]
	b	.LBB99_15
.LBB99_26:
	mov	w8, w23
	sxth	x26, w8
	ldrh	w0, [x27, x26, lsl #1]
	mov	w1, #1
	bl	FtlGetLastWrittenPage
	add	w8, w0, #1                      // =1
	strh	w8, [x19, #2]
	ldr	x8, [sp, #8]                    // 8-byte Folded Reload
	strh	w23, [x19]
	ldr	w8, [x8, x26, lsl #2]
	str	w8, [x19, #48]
	tbnz	w0, #15, .LBB99_40
// %bb.27:
	mov	w23, wzr
	sxth	w25, w0
	b	.LBB99_30
.LBB99_28:                              //   in Loop: Header=BB99_30 Depth=1
	ldur	w2, [x29, #-12]                 // 4-byte Folded Reload
	adrp	x0, .L.str.98
	adrp	x1, .L__func__.FtlMapTblRecovery
	add	x0, x0, :lo12:.L.str.98
	add	x1, x1, :lo12:.L__func__.FtlMapTblRecovery
	mov	w3, w23
	bl	sftl_printk
	adrp	x8, req_sys
	mov	w9, #-1
	str	w9, [x8, :lo12:req_sys]
.LBB99_29:                              //   in Loop: Header=BB99_30 Depth=1
	add	w8, w23, #1                     // =1
	sxth	w23, w8
	cmp	w25, w23
	b.lt	.LBB99_40
.LBB99_30:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB99_34 Depth 2
	ldrh	w8, [x27, x26, lsl #1]
	mov	w1, #1
	mov	x0, x22
	orr	w8, w23, w8, lsl #10
	str	w8, [x22, #4]
	bl	FlashReadPages
	ldr	x8, [x22, #16]
	ldr	w4, [x8, #12]
	ldr	w8, [x22]
	cbz	w4, .LBB99_36
// %bb.31:                              //   in Loop: Header=BB99_30 Depth=1
	cmn	w8, #1                          // =1
	b.eq	.LBB99_36
// %bb.32:                              //   in Loop: Header=BB99_30 Depth=1
	ldrh	w9, [x24, :lo12:c_ftl_nand_byte_pre_page]
	mov	w5, #42982
	movk	w5, #18374, lsl #16
	cbz	x9, .LBB99_35
// %bb.33:                              //   in Loop: Header=BB99_30 Depth=1
	adrp	x10, req_sys+8
	ldr	x10, [x10, :lo12:req_sys+8]
	mov	w5, #42982
	movk	w5, #18374, lsl #16
.LBB99_34:                              //   Parent Loop BB99_30 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldrb	w11, [x10], #1
	lsr	w12, w5, #2
	add	w12, w12, w5, lsl #5
	subs	x9, x9, #1                      // =1
	add	w11, w12, w11
	eor	w5, w11, w5
	b.ne	.LBB99_34
.LBB99_35:                              //   in Loop: Header=BB99_30 Depth=1
	cmp	w4, w5
	b.ne	.LBB99_28
.LBB99_36:                              //   in Loop: Header=BB99_30 Depth=1
	cmn	w8, #1                          // =1
	b.eq	.LBB99_29
// %bb.37:                              //   in Loop: Header=BB99_30 Depth=1
	ldrh	w8, [x21, #8]
	cmp	w8, w28
	b.hs	.LBB99_29
// %bb.38:                              //   in Loop: Header=BB99_30 Depth=1
	ldrh	w9, [x21]
	ldrh	w10, [x19, #4]
	cmp	w9, w10
	b.ne	.LBB99_29
// %bb.39:                              //   in Loop: Header=BB99_30 Depth=1
	adrp	x9, req_sys+4
	ldr	w9, [x9, :lo12:req_sys+4]
	ldur	x10, [x29, #-8]                 // 8-byte Folded Reload
	str	w9, [x10, x8, lsl #2]
	b	.LBB99_29
.LBB99_40:
	mov	x0, x19
	bl	ftl_free_no_use_map_blk
	ldrh	w8, [x19, #2]
	ldrh	w9, [x20, :lo12:c_ftl_nand_page_pre_slc_blk]
	cmp	w8, w9
	b.ne	.LBB99_42
// %bb.41:
	mov	x0, x19
	bl	ftl_map_blk_alloc_new_blk
.LBB99_42:
	mov	x0, x19
	bl	ftl_map_blk_gc
	mov	x0, x19
	bl	ftl_map_blk_gc
	ldp	x20, x19, [sp, #112]            // 16-byte Folded Reload
	ldp	x22, x21, [sp, #96]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #80]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #64]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #48]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #32]             // 16-byte Folded Reload
	mov	w0, wzr
	add	sp, sp, #128                    // =128
	hint	#29
	ret
.Lfunc_end99:
	.size	FtlMapTblRecovery, .Lfunc_end99-FtlMapTblRecovery
                                        // -- End function
	.globl	FtlLoadVonderInfo               // -- Begin function FtlLoadVonderInfo
	.p2align	2
	.type	FtlLoadVonderInfo,@function
FtlLoadVonderInfo:                      // @FtlLoadVonderInfo
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	adrp	x8, c_ftl_nand_max_vendor_blks
	adrp	x10, g_totle_vendor_block
	adrp	x11, c_ftl_nand_vendor_region_num
	ldrh	w8, [x8, :lo12:c_ftl_nand_max_vendor_blks]
	ldrh	w10, [x10, :lo12:g_totle_vendor_block]
	ldrh	w11, [x11, :lo12:c_ftl_nand_vendor_region_num]
	adrp	x0, gVendorBlkInfo
	add	x0, x0, :lo12:gVendorBlkInfo
	mov	w9, #61574
	strh	w9, [x0, #4]
	adrp	x9, p_vendor_block_table
	strh	w8, [x0, #10]
	adrp	x8, p_vendor_block_ver_table
	strh	w10, [x0, #8]
	adrp	x10, p_vendor_block_valid_page_count
	strh	w11, [x0, #6]
	adrp	x11, p_vendor_region_ppn_table
	ldr	x9, [x9, :lo12:p_vendor_block_table]
	ldr	x8, [x8, :lo12:p_vendor_block_ver_table]
	ldr	x10, [x10, :lo12:p_vendor_block_valid_page_count]
	ldr	x11, [x11, :lo12:p_vendor_region_ppn_table]
	mov	x29, sp
	stp	x9, x8, [x0, #16]
	stp	x10, x11, [x0, #32]
	bl	FtlMapTblRecovery
	mov	w0, wzr
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end100:
	.size	FtlLoadVonderInfo, .Lfunc_end100-FtlLoadVonderInfo
                                        // -- End function
	.globl	FtlLoadMapInfo                  // -- Begin function FtlLoadMapInfo
	.p2align	2
	.type	FtlLoadMapInfo,@function
FtlLoadMapInfo:                         // @FtlLoadMapInfo
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-64]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #48]             // 16-byte Folded Spill
	adrp	x20, c_ftl_nand_max_map_blks
	adrp	x19, p_map_block_valid_page_count
	ldr	w8, [x20, :lo12:c_ftl_nand_max_map_blks]
	ldr	x0, [x19, :lo12:p_map_block_valid_page_count]
	mov	w1, wzr
	str	x23, [sp, #16]                  // 8-byte Folded Spill
	lsl	w2, w8, #1
	stp	x22, x21, [sp, #32]             // 16-byte Folded Spill
	mov	x29, sp
	bl	memset
	adrp	x22, c_ftl_nand_byte_pre_page
	adrp	x23, c_ftl_nand_l2pmap_ram_region_num
	adrp	x21, p_l2p_map_buf
	ldrh	w8, [x22, :lo12:c_ftl_nand_byte_pre_page]
	ldrh	w9, [x23, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	ldr	x0, [x21, :lo12:p_l2p_map_buf]
	mov	w1, #255
	mul	x2, x9, x8
	bl	memset
	ldrh	w8, [x23, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	cbz	w8, .LBB101_3
// %bb.1:
	mov	x8, xzr
	mov	x9, xzr
	mov	x10, xzr
	adrp	x11, p_l2p_ram_map
	mov	w12, #65535
	mov	x13, #4294967296
.LBB101_2:                              // =>This Inner Loop Header: Depth=1
	ldr	x14, [x11, :lo12:p_l2p_ram_map]
	add	x10, x10, #1                    // =1
	add	x14, x14, x8
	str	wzr, [x14, #4]
	ldr	x14, [x11, :lo12:p_l2p_ram_map]
	strh	w12, [x14, x8]
	ldrh	w14, [x22, :lo12:c_ftl_nand_byte_pre_page]
	ldr	x15, [x21, :lo12:p_l2p_map_buf]
	ldr	x16, [x11, :lo12:p_l2p_ram_map]
	mul	x14, x9, x14
	asr	x14, x14, #32
	and	x14, x14, #0xfffffffffffffffc
	add	x14, x15, x14
	add	x15, x16, x8
	str	x14, [x15, #8]
	ldrh	w14, [x23, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	add	x9, x9, x13
	add	x8, x8, #16                     // =16
	cmp	x10, x14
	b.lo	.LBB101_2
.LBB101_3:
	adrp	x0, gL2pMapInfo
	add	x0, x0, :lo12:gL2pMapInfo
	mov	w8, #-255655937
	mov	w9, #65535
	stur	w8, [x0, #2]
	adrp	x8, g_totle_map_block
	strh	w9, [x0]
	adrp	x9, c_ftl_nand_map_region_num
	ldr	w10, [x20, :lo12:c_ftl_nand_max_map_blks]
	ldrh	w8, [x8, :lo12:g_totle_map_block]
	ldrh	w9, [x9, :lo12:c_ftl_nand_map_region_num]
	ldr	x11, [x19, :lo12:p_map_block_valid_page_count]
	strh	w10, [x0, #10]
	adrp	x10, p_map_block_table
	strh	w8, [x0, #8]
	adrp	x8, p_map_block_ver_table
	strh	w9, [x0, #6]
	adrp	x9, p_map_region_ppn_table
	ldr	x10, [x10, :lo12:p_map_block_table]
	ldr	x8, [x8, :lo12:p_map_block_ver_table]
	ldr	x9, [x9, :lo12:p_map_region_ppn_table]
	stp	x10, x8, [x0, #16]
	stp	x11, x9, [x0, #32]
	bl	FtlMapTblRecovery
	ldp	x20, x19, [sp, #48]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #32]             // 16-byte Folded Reload
	ldr	x23, [sp, #16]                  // 8-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #64             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end101:
	.size	FtlLoadMapInfo, .Lfunc_end101-FtlLoadMapInfo
                                        // -- End function
	.globl	ftl_sb_update_avl_pages         // -- Begin function ftl_sb_update_avl_pages
	.p2align	2
	.type	ftl_sb_update_avl_pages,@function
ftl_sb_update_avl_pages:                // @ftl_sb_update_avl_pages
// %bb.0:
	hint	#25
	strh	wzr, [x0, #4]
	adrp	x8, c_ftl_nand_planes_num
	ldrh	w8, [x8, :lo12:c_ftl_nand_planes_num]
                                        // kill: def $w2 killed $w2 def $x2
	mov	w9, wzr
	cmp	w8, w2, uxth
	b.ls	.LBB102_5
// %bb.1:
	mov	w9, wzr
	and	x10, x2, #0xffff
	add	x11, x0, #16                    // =16
	b	.LBB102_3
.LBB102_2:                              //   in Loop: Header=BB102_3 Depth=1
	add	x10, x10, #1                    // =1
	cmp	x10, x8
	b.hs	.LBB102_5
.LBB102_3:                              // =>This Inner Loop Header: Depth=1
	ldrsh	w12, [x11, x10, lsl #1]
	cmn	w12, #1                         // =1
	b.eq	.LBB102_2
// %bb.4:                               //   in Loop: Header=BB102_3 Depth=1
	add	w9, w9, #1                      // =1
	strh	w9, [x0, #4]
	b	.LBB102_2
.LBB102_5:
	cbz	w8, .LBB102_10
// %bb.6:
	adrp	x10, c_ftl_nand_page_pre_blk
	ldrh	w10, [x10, :lo12:c_ftl_nand_page_pre_blk]
	mvn	w11, w1
	add	w10, w10, w11
	add	x11, x0, #16                    // =16
	b	.LBB102_8
.LBB102_7:                              //   in Loop: Header=BB102_8 Depth=1
	subs	x8, x8, #1                      // =1
	add	x11, x11, #2                    // =2
	b.eq	.LBB102_10
.LBB102_8:                              // =>This Inner Loop Header: Depth=1
	ldrsh	w12, [x11]
	cmn	w12, #1                         // =1
	b.eq	.LBB102_7
// %bb.9:                               //   in Loop: Header=BB102_8 Depth=1
	add	w9, w10, w9
	strh	w9, [x0, #4]
	b	.LBB102_7
.LBB102_10:
	hint	#29
	ret
.Lfunc_end102:
	.size	ftl_sb_update_avl_pages, .Lfunc_end102-ftl_sb_update_avl_pages
                                        // -- End function
	.globl	FtlReUsePrevPpa                 // -- Begin function FtlReUsePrevPpa
	.p2align	2
	.type	FtlReUsePrevPpa,@function
FtlReUsePrevPpa:                        // @FtlReUsePrevPpa
// %bb.0:
	hint	#25
	sub	sp, sp, #96                     // =96
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #32]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #48]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #64]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #80]             // 16-byte Folded Spill
	adrp	x10, c_ftl_nand_blks_per_die
	str	x8, [sp, #8]
	ldrh	w10, [x10, :lo12:c_ftl_nand_blks_per_die]
	adrp	x11, c_ftl_nand_planes_per_die
	adrp	x21, p_valid_page_count_table
	ldrh	w11, [x11, :lo12:c_ftl_nand_planes_per_die]
	ubfx	w9, w1, #10, #16
	ldr	x8, [x21, :lo12:p_valid_page_count_table]
	udiv	w12, w9, w10
	msub	w9, w12, w10, w9
	str	w1, [sp, #4]
	udiv	w20, w9, w11
	ldrh	w9, [x8, w20, uxtw #1]
	mov	w19, w0
	add	x29, sp, #16                    // =16
	cbz	w9, .LBB103_4
// %bb.1:
	add	w9, w9, #1                      // =1
	strh	w9, [x8, x20, lsl #1]
.LBB103_2:
	add	x1, sp, #4                      // =4
	mov	w2, #1
	mov	w0, w19
	bl	log2phys
	adrp	x9, __stack_chk_guard
	ldr	x8, [sp, #8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB103_26
// %bb.3:
	ldp	x20, x19, [sp, #80]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #64]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #48]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	add	sp, sp, #96                     // =96
	hint	#29
	ret
.LBB103_4:
	adrp	x24, p_free_data_block_list_head
	ldr	x8, [x24, :lo12:p_free_data_block_list_head]
	cbz	x8, .LBB103_2
// %bb.5:
	adrp	x22, g_num_free_superblocks
	ldrh	w9, [x22, :lo12:g_num_free_superblocks]
	cbz	w9, .LBB103_2
// %bb.6:
	adrp	x23, p_data_block_list_table
	ldr	x10, [x23, :lo12:p_data_block_list_table]
	mov	w13, #43691
	movk	w13, #43690, lsl #16
	mov	w11, wzr
	sub	x14, x8, x10
	lsr	x14, x14, #1
	mov	w12, #6
	mul	w14, w14, w13
	mov	w13, #65535
.LBB103_7:                              // =>This Inner Loop Header: Depth=1
	and	w15, w14, #0xffff
	cmp	w15, w20, uxth
	b.eq	.LBB103_10
// %bb.8:                               //   in Loop: Header=BB103_7 Depth=1
	and	x14, x14, #0xffff
	mul	x14, x14, x12
	ldrh	w14, [x10, x14]
	cmp	w14, w13
	b.eq	.LBB103_2
// %bb.9:                               //   in Loop: Header=BB103_7 Depth=1
	add	w11, w11, #1                    // =1
	cmp	w9, w11, uxth
	b.hi	.LBB103_7
	b	.LBB103_2
.LBB103_10:
	mov	w9, #6
	madd	x25, x20, x9, x10
	mov	x26, x25
	ldrh	w9, [x26, #2]!
	cmp	x25, x8
	b.eq	.LBB103_13
// %bb.11:
	mov	w10, #65535
	cmp	w9, w10
	b.ne	.LBB103_13
// %bb.12:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.List_remove_node
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.List_remove_node
	mov	w2, #372
	bl	sftl_printk
	ldr	x8, [x24, :lo12:p_free_data_block_list_head]
.LBB103_13:
	ldrh	w9, [x25]
	cmp	x25, x8
	b.eq	.LBB103_16
// %bb.14:
	mov	w8, #65535
	cmp	w9, w8
	b.eq	.LBB103_18
// %bb.15:
	ldr	x8, [x23, :lo12:p_data_block_list_table]
	ldrh	w10, [x26]
	mov	w11, #6
	madd	x8, x9, x11, x8
	strh	w10, [x8, #2]
	ldrh	w9, [x26]
	ldr	x8, [x23, :lo12:p_data_block_list_table]
	ldrh	w10, [x25]
	mul	x9, x9, x11
	strh	w10, [x8, x9]
	b	.LBB103_21
.LBB103_16:
	mov	w8, #65535
	cmp	w9, w8
	b.eq	.LBB103_20
// %bb.17:
	ldr	x10, [x23, :lo12:p_data_block_list_table]
	mov	w11, #6
	madd	x9, x9, x11, x10
	str	x9, [x24, :lo12:p_free_data_block_list_head]
	strh	w8, [x9, #2]
	b	.LBB103_21
.LBB103_18:
	ldrh	w8, [x26]
	mov	w9, #65535
	cmp	x8, x9
	b.eq	.LBB103_21
// %bb.19:
	ldr	x10, [x23, :lo12:p_data_block_list_table]
	mov	w11, #6
	mul	x8, x8, x11
	strh	w9, [x10, x8]
	b	.LBB103_21
.LBB103_20:
	str	xzr, [x24, :lo12:p_free_data_block_list_head]
.LBB103_21:
	mov	w8, #65535
	strh	w8, [x25]
	strh	w8, [x26]
	ldrh	w8, [x22, :lo12:g_num_free_superblocks]
	cbnz	w8, .LBB103_23
// %bb.22:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlReUsePrevPpa
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlReUsePrevPpa
	mov	w2, #1742
	bl	sftl_printk
	ldrh	w8, [x22, :lo12:g_num_free_superblocks]
.LBB103_23:
	sub	w8, w8, #1                      // =1
	mov	w0, w20
	strh	w8, [x22, :lo12:g_num_free_superblocks]
	bl	insert_data_list
	adrp	x8, g_num_data_superblocks
	ldrh	w9, [x8, :lo12:g_num_data_superblocks]
	adrp	x10, c_ftl_nand_data_blks_per_plane
	ldrh	w10, [x10, :lo12:c_ftl_nand_data_blks_per_plane]
	add	w9, w9, #1                      // =1
	strh	w9, [x8, :lo12:g_num_data_superblocks]
	cmp	w10, w9, uxth
	b.hs	.LBB103_25
// %bb.24:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.INSERT_DATA_LIST
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.INSERT_DATA_LIST
	mov	w2, #214
	bl	sftl_printk
.LBB103_25:
	ldr	x8, [x21, :lo12:p_valid_page_count_table]
	lsl	x9, x20, #1
	ldrh	w10, [x8, x9]
	add	w10, w10, #1                    // =1
	strh	w10, [x8, x9]
	b	.LBB103_2
.LBB103_26:
	bl	__stack_chk_fail
.Lfunc_end103:
	.size	FtlReUsePrevPpa, .Lfunc_end103-FtlReUsePrevPpa
                                        // -- End function
	.globl	FtlRecoverySuperblock           // -- Begin function FtlRecoverySuperblock
	.p2align	2
	.type	FtlRecoverySuperblock,@function
FtlRecoverySuperblock:                  // @FtlRecoverySuperblock
// %bb.0:
	hint	#25
	sub	sp, sp, #160                    // =160
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	stp	x29, x30, [sp, #64]             // 16-byte Folded Spill
	add	x29, sp, #64                    // =64
	stp	x28, x27, [sp, #80]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #96]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #112]            // 16-byte Folded Spill
	stp	x22, x21, [sp, #128]            // 16-byte Folded Spill
	stp	x20, x19, [sp, #144]            // 16-byte Folded Spill
	stur	x8, [x29, #-8]
	ldrsh	w8, [x0]
	cmn	w8, #1                          // =1
	b.eq	.LBB104_148
// %bb.1:
	adrp	x24, c_ftl_nand_page_pre_blk
	ldrh	w25, [x0, #2]
	ldrh	w8, [x24, :lo12:c_ftl_nand_page_pre_blk]
	mov	x19, x0
	cmp	w25, w8
	b.ne	.LBB104_3
// %bb.2:
	strh	wzr, [x19, #4]
	strb	wzr, [x19, #6]
	b	.LBB104_148
.LBB104_3:
	ldrb	w8, [x19, #6]
	ldrh	w0, [x19, #16]
	str	w8, [sp, #32]                   // 4-byte Folded Spill
	mov	w8, #65535
	cmp	w0, w8
	b.eq	.LBB104_5
// %bb.4:
	mov	w21, wzr
	b	.LBB104_7
.LBB104_5:
	mov	w21, wzr
.LBB104_6:                              // =>This Inner Loop Header: Depth=1
	add	w21, w21, #1                    // =1
	add	x9, x19, w21, uxth #1
	ldrh	w0, [x9, #16]
	cmp	w0, w8
	b.eq	.LBB104_6
.LBB104_7:
	mov	w1, #1
	bl	FtlGetLastWrittenPage
	cmn	w0, #1                          // =1
	b.eq	.LBB104_13
// %bb.8:
	adrp	x26, c_ftl_nand_planes_num
	ldrh	w15, [x26, :lo12:c_ftl_nand_planes_num]
	mov	w20, w0
	adrp	x22, req_read
	cbz	w15, .LBB104_19
// %bb.9:
	mov	x8, xzr
	mov	w23, wzr
	add	x9, x19, #16                    // =16
	mov	w10, #65535
	adrp	x11, p_gc_data_buf
	adrp	x12, c_ftl_nand_byte_pre_page
	adrp	x13, p_gc_spare_buf
	adrp	x14, c_ftl_nand_byte_pre_oob
	b	.LBB104_11
.LBB104_10:                             //   in Loop: Header=BB104_11 Depth=1
	add	x8, x8, #1                      // =1
	cmp	x8, w15, uxth
	b.hs	.LBB104_20
.LBB104_11:                             // =>This Inner Loop Header: Depth=1
	ldrh	w16, [x9, x8, lsl #1]
	cmp	w16, w10
	b.eq	.LBB104_10
// %bb.12:                              //   in Loop: Header=BB104_11 Depth=1
	ldr	x15, [x22, :lo12:req_read]
	and	x17, x23, #0xffff
	lsl	x17, x17, #5
	orr	w16, w20, w16, lsl #10
	add	x15, x15, x17
	str	w16, [x15, #4]
	ldrh	w15, [x12, :lo12:c_ftl_nand_byte_pre_page]
	and	w16, w23, #0xffff
	ldr	x18, [x11, :lo12:p_gc_data_buf]
	ldr	x0, [x22, :lo12:req_read]
	mul	w15, w15, w16
	add	w1, w15, #3                     // =3
	cmp	w15, #0                         // =0
	csel	w15, w1, w15, lt
	asr	w15, w15, #2
	add	x0, x0, x17
	add	x15, x18, w15, sxtw #2
	str	x15, [x0, #8]
	ldrh	w15, [x14, :lo12:c_ftl_nand_byte_pre_oob]
	ldr	x18, [x13, :lo12:p_gc_spare_buf]
	ldr	x0, [x22, :lo12:req_read]
	add	w23, w23, #1                    // =1
	mul	w15, w15, w16
	add	w16, w15, #3                    // =3
	cmp	w15, #0                         // =0
	csel	w15, w16, w15, lt
	asr	w15, w15, #2
	add	x15, x18, w15, sxtw #2
	add	x16, x0, x17
	str	x15, [x16, #16]
	ldrh	w15, [x26, :lo12:c_ftl_nand_planes_num]
	b	.LBB104_10
.LBB104_13:
	cbz	w25, .LBB104_15
// %bb.14:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlRecoverySuperblock
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlRecoverySuperblock
	mov	w2, #1809
	bl	sftl_printk
.LBB104_15:
	ldr	w8, [sp, #32]                   // 4-byte Folded Reload
	cbz	w8, .LBB104_18
// %bb.16:
	cmp	w8, w21, uxth
	b.eq	.LBB104_18
// %bb.17:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlRecoverySuperblock
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlRecoverySuperblock
	mov	w2, #1810
	bl	sftl_printk
.LBB104_18:
	strh	wzr, [x19, #2]
	strb	wzr, [x19, #6]
	b	.LBB104_148
.LBB104_19:
	mov	w23, wzr
.LBB104_20:
	ldr	x0, [x22, :lo12:req_read]
	and	w21, w23, #0xffff
	mov	w1, w21
	bl	FlashReadPages
	adrp	x8, g_GlobalDataVersion
	ldr	w8, [x8, :lo12:g_GlobalDataVersion]
	cbz	w21, .LBB104_29
// %bb.21:
	ldr	x13, [x22, :lo12:req_read]
	mov	w9, w23
	mov	x10, xzr
	and	x11, x9, #0xffff
	mov	w9, #65535
	mov	w12, #-2147483648
	add	x13, x13, #16                   // =16
	mov	w14, #-2147483647
	mov	w15, w8
	b	.LBB104_24
.LBB104_22:                             //   in Loop: Header=BB104_24 Depth=1
	mov	w9, w20
.LBB104_23:                             //   in Loop: Header=BB104_24 Depth=1
	add	x10, x10, #1                    // =1
	cmp	x11, x10
	add	x13, x13, #32                   // =32
	b.eq	.LBB104_31
.LBB104_24:                             // =>This Inner Loop Header: Depth=1
	ldur	w16, [x13, #-16]
	cbnz	w16, .LBB104_22
// %bb.25:                              //   in Loop: Header=BB104_24 Depth=1
	ldr	x16, [x13]
	ldr	w17, [x16, #4]
	cmn	w17, #1                         // =1
	b.eq	.LBB104_28
// %bb.26:                              //   in Loop: Header=BB104_24 Depth=1
	subs	w18, w17, w15
	sub	w0, w15, w17
	cmp	w18, w12
	cset	w18, hi
	cmp	w0, w14
	cset	w0, lo
	cmp	w17, w15
	csel	w18, w18, w0, hi
	tbnz	w18, #0, .LBB104_28
// %bb.27:                              //   in Loop: Header=BB104_24 Depth=1
	add	w15, w17, #1                    // =1
	adrp	x17, g_GlobalDataVersion
	str	w15, [x17, :lo12:g_GlobalDataVersion]
.LBB104_28:                             //   in Loop: Header=BB104_24 Depth=1
	ldr	w16, [x16]
	cmn	w16, #1                         // =1
	b.ne	.LBB104_23
	b	.LBB104_30
.LBB104_29:
	mov	w10, wzr
	mov	w9, #65535
.LBB104_30:
	and	w11, w10, #0xffff
	cmp	w11, w23, uxth
	b.ne	.LBB104_32
.LBB104_31:
	ldr	x10, [x22, :lo12:req_read]
	adrp	x11, c_ftl_nand_blks_per_die
	adrp	x12, c_ftl_nand_planes_per_die
	ldrh	w11, [x11, :lo12:c_ftl_nand_blks_per_die]
	ldr	w10, [x10, #4]
	ldrh	w12, [x12, :lo12:c_ftl_nand_planes_per_die]
	add	w14, w20, #1                    // =1
	ubfx	w10, w10, #10, #16
	udiv	w11, w10, w11
	udiv	w13, w10, w12
	b	.LBB104_33
.LBB104_32:
	ldr	x11, [x22, :lo12:req_read]
                                        // kill: def $w10 killed $w10 killed $x10 def $x10
	adrp	x12, c_ftl_nand_planes_per_die
	and	x10, x10, #0xffff
	ldrh	w12, [x12, :lo12:c_ftl_nand_planes_per_die]
	add	x10, x11, x10, lsl #5
	ldr	w10, [x10, #4]
	adrp	x11, c_ftl_nand_blks_per_die
	ldrh	w11, [x11, :lo12:c_ftl_nand_blks_per_die]
	mov	w14, w20
	ubfx	w10, w10, #10, #16
	udiv	w13, w10, w12
	udiv	w11, w10, w11
.LBB104_33:
	mul	w11, w12, w11
	msub	w12, w13, w12, w10
	ldrh	w10, [x24, :lo12:c_ftl_nand_page_pre_blk]
	and	w13, w14, #0xffff
	cmp	w10, w13
	b.ne	.LBB104_35
// %bb.34:
	strh	w14, [x19, #2]
	strb	wzr, [x19, #6]
	strh	wzr, [x19, #4]
.LBB104_35:
	add	w12, w11, w12
	ldr	w11, [sp, #32]                  // 4-byte Folded Reload
	cmp	w13, w25
	b.ne	.LBB104_42
// %bb.36:
	cmp	w11, w12, uxth
	b.ne	.LBB104_42
// %bb.37:
	strh	wzr, [x19, #4]
	ldrh	w8, [x26, :lo12:c_ftl_nand_planes_num]
	mov	w9, wzr
	cmp	w8, w12, uxth
	b.ls	.LBB104_47
// %bb.38:
	sub	x11, x8, w12, uxth
	add	x12, x19, w12, uxth #1
	mov	w9, wzr
	add	x12, x12, #16                   // =16
	b	.LBB104_40
.LBB104_39:                             //   in Loop: Header=BB104_40 Depth=1
	subs	x11, x11, #1                    // =1
	add	x12, x12, #2                    // =2
	b.eq	.LBB104_47
.LBB104_40:                             // =>This Inner Loop Header: Depth=1
	ldrsh	w13, [x12]
	cmn	w13, #1                         // =1
	b.eq	.LBB104_39
// %bb.41:                              //   in Loop: Header=BB104_40 Depth=1
	add	w9, w9, #1                      // =1
	strh	w9, [x19, #4]
	b	.LBB104_39
.LBB104_42:
	sub	w26, w8, #1                     // =1
	mvn	w8, w9
	tst	w8, #0xffff
	str	x12, [sp, #24]                  // 8-byte Folded Spill
	stp	w25, w14, [x29, #-28]           // 8-byte Folded Spill
	b.eq	.LBB104_67
.LBB104_43:
	adrp	x9, g_recovery_page_min_ver
	ldr	w8, [x9, :lo12:g_recovery_page_min_ver]
	cmn	w8, #1                          // =1
	b.ne	.LBB104_45
// %bb.44:
	str	w26, [x9, :lo12:g_recovery_page_min_ver]
.LBB104_45:
	ldur	w10, [x29, #-28]                // 4-byte Folded Reload
	and	w21, w20, #0xffff
	sub	w9, w20, #7                     // =7
	add	w8, w10, #7                     // =7
	cmp	w21, w8
	csel	w8, w9, w10, hi
	and	w23, w8, #0xffff
	cmp	w21, w23
	b.hs	.LBB104_52
.LBB104_46:
	mov	w8, #-1
	adrp	x9, g_recovery_page_min_ver
	str	w8, [x9, :lo12:g_recovery_page_min_ver]
	b	.LBB104_68
.LBB104_47:
	cbz	w8, .LBB104_148
// %bb.48:
	mvn	w11, w14
	add	w10, w10, w11
	add	x11, x19, #16                   // =16
	b	.LBB104_50
.LBB104_49:                             //   in Loop: Header=BB104_50 Depth=1
	subs	x8, x8, #1                      // =1
	add	x11, x11, #2                    // =2
	b.eq	.LBB104_148
.LBB104_50:                             // =>This Inner Loop Header: Depth=1
	ldrsh	w12, [x11]
	cmn	w12, #1                         // =1
	b.eq	.LBB104_49
// %bb.51:                              //   in Loop: Header=BB104_50 Depth=1
	add	w9, w10, w9
	strh	w9, [x19, #4]
	b	.LBB104_49
.LBB104_52:
	add	x24, x19, #16                   // =16
	mov	w25, #65535
	b	.LBB104_54
.LBB104_53:                             //   in Loop: Header=BB104_54 Depth=1
	add	w8, w23, #1                     // =1
	and	w23, w8, #0xffff
	cmp	w21, w23
	b.lo	.LBB104_46
.LBB104_54:                             // =>This Loop Header: Depth=1
                                        //     Child Loop BB104_57 Depth 2
                                        //     Child Loop BB104_63 Depth 2
	adrp	x8, c_ftl_nand_planes_num
	ldrh	w9, [x8, :lo12:c_ftl_nand_planes_num]
	cbz	w9, .LBB104_59
// %bb.55:                              //   in Loop: Header=BB104_54 Depth=1
	mov	x8, xzr
	mov	w27, wzr
	b	.LBB104_57
.LBB104_56:                             //   in Loop: Header=BB104_57 Depth=2
	add	x8, x8, #1                      // =1
	cmp	x8, w9, uxth
	b.hs	.LBB104_60
.LBB104_57:                             //   Parent Loop BB104_54 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldrh	w10, [x24, x8, lsl #1]
	cmp	w10, w25
	b.eq	.LBB104_56
// %bb.58:                              //   in Loop: Header=BB104_57 Depth=2
	ldr	x9, [x22, :lo12:req_read]
	and	x11, x27, #0xffff
	orr	w10, w23, w10, lsl #10
	add	w27, w27, #1                    // =1
	add	x9, x9, x11, lsl #5
	str	w10, [x9, #4]
	adrp	x9, c_ftl_nand_planes_num
	ldrh	w9, [x9, :lo12:c_ftl_nand_planes_num]
	b	.LBB104_56
.LBB104_59:                             //   in Loop: Header=BB104_54 Depth=1
	mov	w27, wzr
.LBB104_60:                             //   in Loop: Header=BB104_54 Depth=1
	ldr	x0, [x22, :lo12:req_read]
	and	w20, w27, #0xffff
	mov	w1, w20
	bl	FlashReadPages
	cbz	w20, .LBB104_53
// %bb.61:                              //   in Loop: Header=BB104_54 Depth=1
	ldr	x9, [x22, :lo12:req_read]
                                        // kill: def $w27 killed $w27 killed $x27 def $x27
	and	x8, x27, #0xffff
	add	x9, x9, #16                     // =16
	b	.LBB104_63
.LBB104_62:                             //   in Loop: Header=BB104_63 Depth=2
	subs	x8, x8, #1                      // =1
	add	x9, x9, #32                     // =32
	b.eq	.LBB104_53
.LBB104_63:                             //   Parent Loop BB104_54 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldur	w10, [x9, #-16]
	cbnz	w10, .LBB104_68
// %bb.64:                              //   in Loop: Header=BB104_63 Depth=2
	ldr	x10, [x9]
	ldrsh	w11, [x10]
	cmn	w11, #1                         // =1
	b.eq	.LBB104_62
// %bb.65:                              //   in Loop: Header=BB104_63 Depth=2
	ldr	w10, [x10, #4]
	cmn	w10, #1                         // =1
	b.eq	.LBB104_62
// %bb.66:                              //   in Loop: Header=BB104_63 Depth=2
	adrp	x11, g_recovery_page_min_ver
	str	w10, [x11, :lo12:g_recovery_page_min_ver]
	b	.LBB104_62
.LBB104_67:
	ldrb	w8, [x19, #8]
	cbz	w8, .LBB104_43
.LBB104_68:
	ldur	w25, [x29, #-28]                // 4-byte Folded Reload
	adrp	x8, g_power_lost_recovery_flag
	mov	w9, #1
	add	x20, x19, #16                   // =16
	mov	w14, #65535
	mov	w28, w25
	strh	w9, [x8, :lo12:g_power_lost_recovery_flag]
	b	.LBB104_70
.LBB104_69:                             //   in Loop: Header=BB104_70 Depth=1
	adrp	x8, c_ftl_nand_page_pre_blk
	ldrh	w8, [x8, :lo12:c_ftl_nand_page_pre_blk]
	add	w28, w28, #1                    // =1
	mov	w14, #65535
	cmp	w8, w28, uxth
	b.eq	.LBB104_143
.LBB104_70:                             // =>This Loop Header: Depth=1
                                        //     Child Loop BB104_73 Depth 2
                                        //     Child Loop BB104_80 Depth 2
	adrp	x13, c_ftl_nand_planes_num
	ldrh	w10, [x13, :lo12:c_ftl_nand_planes_num]
	cbz	w10, .LBB104_75
// %bb.71:                              //   in Loop: Header=BB104_70 Depth=1
	mov	x8, xzr
	mov	w21, wzr
	and	w9, w28, #0xffff
	b	.LBB104_73
.LBB104_72:                             //   in Loop: Header=BB104_73 Depth=2
	add	x8, x8, #1                      // =1
	cmp	x8, w10, uxth
	b.hs	.LBB104_76
.LBB104_73:                             //   Parent Loop BB104_70 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldrh	w11, [x20, x8, lsl #1]
	cmp	w11, w14
	b.eq	.LBB104_72
// %bb.74:                              //   in Loop: Header=BB104_73 Depth=2
	ldr	x10, [x22, :lo12:req_read]
	and	x12, x21, #0xffff
	orr	w11, w9, w11, lsl #10
	add	w21, w21, #1                    // =1
	add	x10, x10, x12, lsl #5
	str	w11, [x10, #4]
	ldrh	w10, [x13, :lo12:c_ftl_nand_planes_num]
	b	.LBB104_72
.LBB104_75:                             //   in Loop: Header=BB104_70 Depth=1
	mov	w21, wzr
.LBB104_76:                             //   in Loop: Header=BB104_70 Depth=1
	ldr	x0, [x22, :lo12:req_read]
	and	w24, w21, #0xffff
	mov	w1, w24
	bl	FlashReadPages
	cbz	w24, .LBB104_69
// %bb.77:                              //   in Loop: Header=BB104_70 Depth=1
	ldr	w24, [sp, #32]                  // 4-byte Folded Reload
                                        // kill: def $w21 killed $w21 killed $x21 def $x21
	and	x8, x21, #0xffff
	mov	x27, xzr
	lsl	x23, x8, #5
	b	.LBB104_80
.LBB104_78:                             //   in Loop: Header=BB104_80 Depth=2
	str	w26, [x9, :lo12:g_recovery_page_min_ver]
.LBB104_79:                             //   in Loop: Header=BB104_80 Depth=2
	add	x27, x27, #32                   // =32
	cmp	x23, x27
	b.eq	.LBB104_69
.LBB104_80:                             //   Parent Loop BB104_70 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldr	x8, [x22, :lo12:req_read]
	adrp	x10, c_ftl_nand_planes_per_die
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_per_die]
	and	w11, w28, #0xffff
	add	x9, x8, x27
	ldr	w21, [x9, #4]
	adrp	x9, c_ftl_nand_blks_per_die
	ldrh	w9, [x9, :lo12:c_ftl_nand_blks_per_die]
	ubfx	w12, w21, #10, #16
	udiv	w13, w12, w10
	udiv	w9, w12, w9
	msub	w12, w13, w10, w12
	madd	w9, w10, w9, w12
	cmp	w24, w9, uxth
	cset	w10, hi
	cmp	w11, w25
	cset	w11, eq
	stur	w21, [x29, #-20]
	b.lo	.LBB104_79
// %bb.81:                              //   in Loop: Header=BB104_80 Depth=2
	and	w10, w11, w10
	tbnz	w10, #0, .LBB104_79
// %bb.82:                              //   in Loop: Header=BB104_80 Depth=2
	ldur	w11, [x29, #-24]                // 4-byte Folded Reload
	and	w10, w28, #0xffff
	cmp	w10, w11, uxth
	b.ne	.LBB104_84
// %bb.83:                              //   in Loop: Header=BB104_80 Depth=2
	ldr	x10, [sp, #24]                  // 8-byte Folded Reload
	and	w9, w9, #0xffff
	cmp	w9, w10, uxth
	b.eq	.LBB104_133
.LBB104_84:                             //   in Loop: Header=BB104_80 Depth=2
	ldr	w9, [x8, x27]
	cmn	w9, #1                          // =1
	b.eq	.LBB104_98
// %bb.85:                              //   in Loop: Header=BB104_80 Depth=2
	add	x8, x8, x27
	ldr	x25, [x8, #16]
	ldrsh	w8, [x25]
	cmn	w8, #3947                       // =3947
	b.ne	.LBB104_102
// %bb.86:                              //   in Loop: Header=BB104_80 Depth=2
	ldr	w26, [x25, #4]
	cmn	w26, #1                         // =1
	b.eq	.LBB104_90
// %bb.87:                              //   in Loop: Header=BB104_80 Depth=2
	adrp	x8, g_GlobalDataVersion
	ldr	w8, [x8, :lo12:g_GlobalDataVersion]
	mov	w11, #-2147483648
	subs	w9, w26, w8
	sub	w10, w8, w26
	cmp	w9, w11
	mov	w11, #-2147483647
	cset	w9, hi
	cmp	w10, w11
	cset	w10, lo
	cmp	w26, w8
	csel	w8, w9, w10, hi
	tbnz	w8, #0, .LBB104_90
// %bb.88:                              //   in Loop: Header=BB104_80 Depth=2
	add	w8, w26, #1                     // =1
	adrp	x9, g_GlobalDataVersion
	str	w8, [x9, :lo12:g_GlobalDataVersion]
	ldrsh	w8, [x25]
	cmn	w8, #3947                       // =3947
	b.eq	.LBB104_90
// %bb.89:                              //   in Loop: Header=BB104_80 Depth=2
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlRecoverySuperblock
	mov	w2, #1961
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlRecoverySuperblock
	bl	sftl_printk
.LBB104_90:                             //   in Loop: Header=BB104_80 Depth=2
	ldp	w0, w24, [x25, #8]
	sub	x1, x29, #16                    // =16
	mov	w2, wzr
	stur	w24, [x29, #-12]
	str	w0, [sp, #20]                   // 4-byte Folded Spill
	bl	log2phys
	adrp	x8, g_recovery_page_min_ver
	ldr	w8, [x8, :lo12:g_recovery_page_min_ver]
	cmn	w8, #1                          // =1
	b.eq	.LBB104_92
// %bb.91:                              //   in Loop: Header=BB104_80 Depth=2
	subs	w9, w26, w8
	mov	w11, #-2147483648
	sub	w10, w8, w26
	cmp	w9, w11
	mov	w11, #-2147483647
	cset	w9, hi
	cmp	w10, w11
	cset	w10, lo
	cmp	w26, w8
	csel	w8, w9, w10, hi
	tbz	w8, #0, .LBB104_114
.LBB104_92:                             //   in Loop: Header=BB104_80 Depth=2
	ldur	w25, [x29, #-16]
	cmp	w25, w21
	b.ne	.LBB104_104
.LBB104_93:                             //   in Loop: Header=BB104_80 Depth=2
	ldur	w25, [x29, #-28]                // 4-byte Folded Reload
	cmn	w24, #1                         // =1
	b.eq	.LBB104_120
// %bb.94:                              //   in Loop: Header=BB104_80 Depth=2
	adrp	x8, c_ftl_nand_blks_per_die
	ldrh	w8, [x8, :lo12:c_ftl_nand_blks_per_die]
	adrp	x10, c_ftl_nand_planes_per_die
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_per_die]
	adrp	x11, c_ftl_nand_data_blks_per_plane
	ubfx	w9, w24, #10, #16
	ldrh	w11, [x11, :lo12:c_ftl_nand_data_blks_per_plane]
	udiv	w12, w9, w8
	msub	w8, w12, w8, w9
	udiv	w24, w8, w10
	cmp	w24, w11
	b.lo	.LBB104_96
// %bb.95:                              //   in Loop: Header=BB104_80 Depth=2
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlRecoverySuperblock
	mov	w2, #2066
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlRecoverySuperblock
	bl	sftl_printk
.LBB104_96:                             //   in Loop: Header=BB104_80 Depth=2
	adrp	x8, p_valid_page_count_table
	ldr	x8, [x8, :lo12:p_valid_page_count_table]
	and	x9, x24, #0xffff
	ldrh	w8, [x8, x9, lsl #1]
	cbz	w8, .LBB104_121
// %bb.97:                              //   in Loop: Header=BB104_80 Depth=2
	mov	w0, w24
	bl	decrement_vpc_count
	ldr	w24, [sp, #32]                  // 4-byte Folded Reload
	b	.LBB104_79
.LBB104_98:                             //   in Loop: Header=BB104_80 Depth=2
	adrp	x8, g_recovery_page_num
	ldr	w8, [x8, :lo12:g_recovery_page_num]
	cmp	w8, #31                         // =31
	b.hi	.LBB104_100
// %bb.99:                              //   in Loop: Header=BB104_80 Depth=2
	adrp	x9, g_recovery_ppa_tbl
	add	x9, x9, :lo12:g_recovery_ppa_tbl
	str	w21, [x9, x8, lsl #2]
	add	w8, w8, #1                      // =1
	adrp	x9, g_recovery_page_num
	str	w8, [x9, :lo12:g_recovery_page_num]
.LBB104_100:                            //   in Loop: Header=BB104_80 Depth=2
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	adrp	x9, g_recovery_page_min_ver
	ldr	w8, [x9, :lo12:g_recovery_page_min_ver]
	cmn	w8, #1                          // =1
	b.eq	.LBB104_78
// %bb.101:                             //   in Loop: Header=BB104_80 Depth=2
	cmp	w8, w26
	b.ls	.LBB104_79
	b	.LBB104_78
.LBB104_102:                            //   in Loop: Header=BB104_80 Depth=2
	ldrh	w0, [x19]
	bl	decrement_vpc_count
.LBB104_103:                            //   in Loop: Header=BB104_80 Depth=2
	ldur	w25, [x29, #-28]                // 4-byte Folded Reload
	b	.LBB104_79
.LBB104_104:                            //   in Loop: Header=BB104_80 Depth=2
	cmn	w24, #1                         // =1
	b.eq	.LBB104_106
// %bb.105:                             //   in Loop: Header=BB104_80 Depth=2
	adrp	x8, c_ftl_nand_totle_phy_blks
	ldr	w8, [x8, :lo12:c_ftl_nand_totle_phy_blks]
	cmp	w8, w24, lsr #10
	b.ls	.LBB104_124
.LBB104_106:                            //   in Loop: Header=BB104_80 Depth=2
	ldr	w0, [sp, #20]                   // 4-byte Folded Reload
	sub	x1, x29, #20                    // =20
	mov	w2, #1
	bl	log2phys
	cmn	w25, #1                         // =1
	b.eq	.LBB104_93
// %bb.107:                             //   in Loop: Header=BB104_80 Depth=2
	cmp	w25, w24
	b.eq	.LBB104_93
// %bb.108:                             //   in Loop: Header=BB104_80 Depth=2
	adrp	x8, c_ftl_nand_blks_per_die
	ldrh	w8, [x8, :lo12:c_ftl_nand_blks_per_die]
	adrp	x10, c_ftl_nand_planes_per_die
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_per_die]
	adrp	x11, g_active_superblock
	ubfx	w9, w25, #10, #16
	ldrh	w11, [x11, :lo12:g_active_superblock]
	udiv	w12, w9, w8
	msub	w8, w12, w8, w9
	udiv	w8, w8, w10
	cmp	w8, w11
	b.eq	.LBB104_111
// %bb.109:                             //   in Loop: Header=BB104_80 Depth=2
	adrp	x9, g_buffer_superblock
	ldrh	w9, [x9, :lo12:g_buffer_superblock]
	cmp	w9, w8, uxth
	b.eq	.LBB104_111
// %bb.110:                             //   in Loop: Header=BB104_80 Depth=2
	adrp	x9, g_gc_temp_superblock
	ldrh	w9, [x9, :lo12:g_gc_temp_superblock]
	cmp	w9, w8, uxth
	b.ne	.LBB104_93
.LBB104_111:                            //   in Loop: Header=BB104_80 Depth=2
	ldr	x8, [x22, :lo12:req_read]
	mov	w1, #1
	str	w25, [x8, #4]
	ldr	x0, [x22, :lo12:req_read]
	ldr	x21, [x8, #16]
	bl	FlashReadPages
	ldr	x8, [x22, :lo12:req_read]
	ldr	w8, [x8]
	cmn	w8, #1                          // =1
	b.eq	.LBB104_93
// %bb.112:                             //   in Loop: Header=BB104_80 Depth=2
	ldr	w8, [x21, #4]
	mov	w11, #-2147483648
	subs	w9, w26, w8
	sub	w10, w8, w26
	cmp	w9, w11
	mov	w11, #-2147483647
	cset	w9, hi
	cmp	w10, w11
	cset	w10, lo
	cmp	w26, w8
	csel	w8, w9, w10, hi
	cmp	w8, #1                          // =1
	b.ne	.LBB104_93
// %bb.113:                             //   in Loop: Header=BB104_80 Depth=2
	ldr	w0, [sp, #20]                   // 4-byte Folded Reload
	sub	x1, x29, #16                    // =16
	mov	w2, #1
	bl	log2phys
	b	.LBB104_93
.LBB104_114:                            //   in Loop: Header=BB104_80 Depth=2
	cmn	w24, #1                         // =1
	b.eq	.LBB104_122
// %bb.115:                             //   in Loop: Header=BB104_80 Depth=2
	ldr	x8, [x22, :lo12:req_read]
	mov	w1, #1
	add	x8, x8, x27
	str	w24, [x8, #4]
	ldr	x9, [x22, :lo12:req_read]
	ldr	x25, [x8, #16]
	add	x0, x9, x27
	bl	FlashReadPages
	ldr	x8, [x22, :lo12:req_read]
	mov	x13, x25
	ldur	w25, [x29, #-28]                // 4-byte Folded Reload
	ldr	w9, [x8, x27]
	cmn	w9, #1                          // =1
	b.eq	.LBB104_126
// %bb.116:                             //   in Loop: Header=BB104_80 Depth=2
	ldr	w9, [x13, #8]
	ldr	w10, [sp, #20]                  // 4-byte Folded Reload
	cmp	w9, w10
	b.ne	.LBB104_126
// %bb.117:                             //   in Loop: Header=BB104_80 Depth=2
	adrp	x9, g_recovery_page_min_ver
	ldr	w14, [x13, #4]
	ldr	w9, [x9, :lo12:g_recovery_page_min_ver]
	mov	w12, #-2147483648
	subs	w10, w9, w14
	sub	w11, w14, w9
	cmp	w10, w12
	mov	w12, #-2147483647
	cset	w10, hi
	cmp	w11, w12
	cset	w11, lo
	cmp	w9, w14
	csel	w9, w10, w11, hi
	tbnz	w9, #0, .LBB104_126
// %bb.118:                             //   in Loop: Header=BB104_80 Depth=2
	ldur	w9, [x29, #-16]
	cmp	w9, w21
	b.ne	.LBB104_125
.LBB104_119:                            //   in Loop: Header=BB104_80 Depth=2
	ldr	w0, [sp, #20]                   // 4-byte Folded Reload
	mov	w1, w24
	bl	FtlReUsePrevPpa
	b	.LBB104_126
.LBB104_120:                            //   in Loop: Header=BB104_80 Depth=2
	ldr	w24, [sp, #32]                  // 4-byte Folded Reload
	b	.LBB104_79
.LBB104_121:                            //   in Loop: Header=BB104_80 Depth=2
	adrp	x0, .L.str.101
	add	x0, x0, :lo12:.L.str.101
	mov	w1, w24
	bl	sftl_printk
	ldr	w24, [sp, #32]                  // 4-byte Folded Reload
	b	.LBB104_79
.LBB104_122:                            //   in Loop: Header=BB104_80 Depth=2
	ldur	w8, [x29, #-16]
	ldr	w24, [sp, #32]                  // 4-byte Folded Reload
	ldur	w25, [x29, #-28]                // 4-byte Folded Reload
	cmp	w8, w21
	b.ne	.LBB104_79
// %bb.123:                             //   in Loop: Header=BB104_80 Depth=2
	ldr	w0, [sp, #20]                   // 4-byte Folded Reload
	sub	x1, x29, #12                    // =12
	mov	w2, #1
	bl	log2phys
	b	.LBB104_79
.LBB104_124:                            //   in Loop: Header=BB104_80 Depth=2
	adrp	x0, .L.str.100
	add	x0, x0, :lo12:.L.str.100
	mov	w1, w24
	bl	sftl_printk
	ldr	w24, [sp, #32]                  // 4-byte Folded Reload
	b	.LBB104_103
.LBB104_125:                            //   in Loop: Header=BB104_80 Depth=2
	cmp	w9, w24
	b.ne	.LBB104_127
.LBB104_126:                            //   in Loop: Header=BB104_80 Depth=2
	ldr	w24, [sp, #32]                  // 4-byte Folded Reload
	mov	w8, #-1
	stur	w8, [x29, #-12]
	b	.LBB104_79
.LBB104_127:                            //   in Loop: Header=BB104_80 Depth=2
	cmn	w9, #1                          // =1
	b.eq	.LBB104_129
// %bb.128:                             //   in Loop: Header=BB104_80 Depth=2
	add	x8, x8, x27
	str	w9, [x8, #4]
	ldr	x9, [x22, :lo12:req_read]
	ldr	x8, [x8, #16]
	mov	w1, #1
	mov	w21, w14
	add	x0, x9, x27
	str	x8, [sp, #8]                    // 8-byte Folded Spill
	bl	FlashReadPages
	mov	w14, w21
	b	.LBB104_130
.LBB104_129:                            //   in Loop: Header=BB104_80 Depth=2
	str	x13, [sp, #8]                   // 8-byte Folded Spill
	str	w9, [x8, x27]
.LBB104_130:                            //   in Loop: Header=BB104_80 Depth=2
	ldr	x8, [x22, :lo12:req_read]
	adrp	x9, g_recovery_page_min_ver
	mov	w12, #-2147483648
	mov	w13, #-2147483647
	ldr	w8, [x8, x27]
	cmn	w8, #1                          // =1
	b.eq	.LBB104_119
// %bb.131:                             //   in Loop: Header=BB104_80 Depth=2
	ldr	x8, [sp, #8]                    // 8-byte Folded Reload
	ldr	w9, [x9, :lo12:g_recovery_page_min_ver]
	ldr	w8, [x8, #4]
	subs	w10, w9, w8
	sub	w11, w8, w9
	cmp	w10, w12
	cset	w10, hi
	cmp	w11, w13
	cset	w11, lo
	cmp	w9, w8
	csel	w9, w10, w11, hi
	tbnz	w9, #0, .LBB104_119
// %bb.132:                             //   in Loop: Header=BB104_80 Depth=2
	subs	w9, w14, w8
	sub	w10, w8, w14
	cmp	w9, w12
	cset	w9, hi
	cmp	w10, w13
	cset	w10, lo
	cmp	w14, w8
	csel	w8, w9, w10, hi
	tbz	w8, #0, .LBB104_119
	b	.LBB104_126
.LBB104_133:
	ldr	x11, [sp, #24]                  // 8-byte Folded Reload
	ldur	w14, [x29, #-24]                // 4-byte Folded Reload
	strh	wzr, [x19, #4]
	adrp	x8, c_ftl_nand_planes_num
	strb	w11, [x19, #6]
	strh	w14, [x19, #2]
	ldrh	w8, [x8, :lo12:c_ftl_nand_planes_num]
	mov	w9, wzr
	adrp	x13, c_ftl_nand_page_pre_blk
	cmp	w8, w11, uxth
	b.ls	.LBB104_138
// %bb.134:
	sub	x10, x8, w11, uxth
	add	x11, x19, w11, uxth #1
	mov	w9, wzr
	add	x11, x11, #16                   // =16
	b	.LBB104_136
.LBB104_135:                            //   in Loop: Header=BB104_136 Depth=1
	subs	x10, x10, #1                    // =1
	add	x11, x11, #2                    // =2
	b.eq	.LBB104_138
.LBB104_136:                            // =>This Inner Loop Header: Depth=1
	ldrsh	w12, [x11]
	cmn	w12, #1                         // =1
	b.eq	.LBB104_135
// %bb.137:                             //   in Loop: Header=BB104_136 Depth=1
	add	w9, w9, #1                      // =1
	strh	w9, [x19, #4]
	b	.LBB104_135
.LBB104_138:
	cbz	w8, .LBB104_148
// %bb.139:
	ldrh	w10, [x13, :lo12:c_ftl_nand_page_pre_blk]
	mvn	w11, w14
	add	w10, w10, w11
	add	x11, x19, #16                   // =16
	b	.LBB104_141
.LBB104_140:                            //   in Loop: Header=BB104_141 Depth=1
	subs	x8, x8, #1                      // =1
	add	x11, x11, #2                    // =2
	b.eq	.LBB104_148
.LBB104_141:                            // =>This Inner Loop Header: Depth=1
	ldrsh	w12, [x11]
	cmn	w12, #1                         // =1
	b.eq	.LBB104_140
// %bb.142:                             //   in Loop: Header=BB104_141 Depth=1
	add	w9, w10, w9
	strh	w9, [x19, #4]
	b	.LBB104_140
.LBB104_143:
	strh	w28, [x19, #2]
	strh	wzr, [x19, #4]
	adrp	x8, c_ftl_nand_planes_num
	ldrh	w8, [x8, :lo12:c_ftl_nand_planes_num]
	cbz	x8, .LBB104_148
// %bb.144:
	mov	x9, xzr
	add	x10, x19, #16                   // =16
.LBB104_145:                            // =>This Inner Loop Header: Depth=1
	ldrsh	w11, [x10, x9, lsl #1]
	cmn	w11, #1                         // =1
	b.ne	.LBB104_147
// %bb.146:                             //   in Loop: Header=BB104_145 Depth=1
	add	x9, x9, #1                      // =1
	cmp	x8, x9
	b.ne	.LBB104_145
	b	.LBB104_148
.LBB104_147:
	strb	w9, [x19, #6]
.LBB104_148:
	adrp	x9, __stack_chk_guard
	ldur	x8, [x29, #-8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB104_150
// %bb.149:
	ldp	x20, x19, [sp, #144]            // 16-byte Folded Reload
	ldp	x22, x21, [sp, #128]            // 16-byte Folded Reload
	ldp	x24, x23, [sp, #112]            // 16-byte Folded Reload
	ldp	x26, x25, [sp, #96]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #80]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #64]             // 16-byte Folded Reload
	mov	w0, wzr
	add	sp, sp, #160                    // =160
	hint	#29
	ret
.LBB104_150:
	bl	__stack_chk_fail
.Lfunc_end104:
	.size	FtlRecoverySuperblock, .Lfunc_end104-FtlRecoverySuperblock
                                        // -- End function
	.globl	FtlSlcSuperblockCheck           // -- Begin function FtlSlcSuperblockCheck
	.p2align	2
	.type	FtlSlcSuperblockCheck,@function
FtlSlcSuperblockCheck:                  // @FtlSlcSuperblockCheck
// %bb.0:
	hint	#25
	ldrh	w8, [x0, #4]
	cbz	w8, .LBB105_3
// %bb.1:
	ldrsh	w8, [x0]
	cmn	w8, #1                          // =1
	b.eq	.LBB105_3
// %bb.2:
	ldrb	w9, [x0, #6]
	add	x8, x0, x9, lsl #1
	ldrsh	w8, [x8, #16]
	cmn	w8, #1                          // =1
	b.eq	.LBB105_4
.LBB105_3:
	hint	#29
	ret
.LBB105_4:
	adrp	x8, c_ftl_nand_planes_num
	ldrh	w8, [x8, :lo12:c_ftl_nand_planes_num]
                                        // kill: def $w9 killed $w9 killed $x9 def $x9
	b	.LBB105_6
.LBB105_5:                              //   in Loop: Header=BB105_6 Depth=1
	add	x10, x0, w9, uxtb #1
	ldrsh	w10, [x10, #16]
	cmn	w10, #1                         // =1
	b.ne	.LBB105_8
.LBB105_6:                              // =>This Inner Loop Header: Depth=1
	add	w9, w9, #1                      // =1
	cmp	w8, w9, uxtb
	b.ne	.LBB105_5
// %bb.7:                               //   in Loop: Header=BB105_6 Depth=1
	ldrh	w10, [x0, #2]
	mov	w9, wzr
	add	w10, w10, #1                    // =1
	strh	w10, [x0, #2]
	b	.LBB105_5
.LBB105_8:
	strb	w9, [x0, #6]
	hint	#29
	ret
.Lfunc_end105:
	.size	FtlSlcSuperblockCheck, .Lfunc_end105-FtlSlcSuperblockCheck
                                        // -- End function
	.globl	FtlPowerLostRecovery            // -- Begin function FtlPowerLostRecovery
	.p2align	2
	.type	FtlPowerLostRecovery,@function
FtlPowerLostRecovery:                   // @FtlPowerLostRecovery
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #16]             // 16-byte Folded Spill
	adrp	x19, g_active_superblock
	add	x19, x19, :lo12:g_active_superblock
	adrp	x8, g_recovery_page_num
	mov	x0, x19
	mov	x29, sp
	str	wzr, [x8, :lo12:g_recovery_page_num]
	bl	FtlRecoverySuperblock
	ldrh	w8, [x19, #4]
	cbz	w8, .LBB106_8
// %bb.1:
	ldrh	w8, [x19]
	mov	w9, #65535
	cmp	w8, w9
	b.eq	.LBB106_8
// %bb.2:
	ldrb	w9, [x19, #6]
	add	x8, x19, x9, lsl #1
	ldrsh	w8, [x8, #16]
	cmn	w8, #1                          // =1
	b.ne	.LBB106_8
// %bb.3:
	adrp	x8, c_ftl_nand_planes_num
	ldrh	w8, [x8, :lo12:c_ftl_nand_planes_num]
	adrp	x10, g_active_superblock+2
                                        // kill: def $w9 killed $w9 killed $x9 def $x9
	b	.LBB106_5
.LBB106_4:                              //   in Loop: Header=BB106_5 Depth=1
	add	x11, x19, w9, uxtb #1
	ldrsh	w11, [x11, #16]
	cmn	w11, #1                         // =1
	b.ne	.LBB106_7
.LBB106_5:                              // =>This Inner Loop Header: Depth=1
	add	w9, w9, #1                      // =1
	cmp	w8, w9, uxtb
	b.ne	.LBB106_4
// %bb.6:                               //   in Loop: Header=BB106_5 Depth=1
	ldrh	w11, [x10, :lo12:g_active_superblock+2]
	mov	w9, wzr
	add	w11, w11, #1                    // =1
	strh	w11, [x10, :lo12:g_active_superblock+2]
	b	.LBB106_4
.LBB106_7:
	adrp	x8, g_active_superblock+6
	strb	w9, [x8, :lo12:g_active_superblock+6]
.LBB106_8:
	adrp	x19, g_buffer_superblock
	add	x19, x19, :lo12:g_buffer_superblock
	mov	x0, x19
	bl	FtlRecoverySuperblock
	ldrh	w8, [x19, #4]
	cbz	w8, .LBB106_16
// %bb.9:
	ldrh	w8, [x19]
	mov	w9, #65535
	cmp	w8, w9
	b.eq	.LBB106_16
// %bb.10:
	ldrb	w9, [x19, #6]
	add	x8, x19, x9, lsl #1
	ldrsh	w8, [x8, #16]
	cmn	w8, #1                          // =1
	b.ne	.LBB106_16
// %bb.11:
	adrp	x8, c_ftl_nand_planes_num
	ldrh	w8, [x8, :lo12:c_ftl_nand_planes_num]
	adrp	x10, g_buffer_superblock+2
                                        // kill: def $w9 killed $w9 killed $x9 def $x9
	b	.LBB106_13
.LBB106_12:                             //   in Loop: Header=BB106_13 Depth=1
	add	x11, x19, w9, uxtb #1
	ldrsh	w11, [x11, #16]
	cmn	w11, #1                         // =1
	b.ne	.LBB106_15
.LBB106_13:                             // =>This Inner Loop Header: Depth=1
	add	w9, w9, #1                      // =1
	cmp	w8, w9, uxtb
	b.ne	.LBB106_12
// %bb.14:                              //   in Loop: Header=BB106_13 Depth=1
	ldrh	w11, [x10, :lo12:g_buffer_superblock+2]
	mov	w9, wzr
	add	w11, w11, #1                    // =1
	strh	w11, [x10, :lo12:g_buffer_superblock+2]
	b	.LBB106_12
.LBB106_15:
	adrp	x8, g_buffer_superblock+6
	strb	w9, [x8, :lo12:g_buffer_superblock+6]
.LBB106_16:
	bl	FtlGcPageRecovery
	adrp	x19, g_tmp_data_superblock_id
	ldrh	w0, [x19, :lo12:g_tmp_data_superblock_id]
	mov	w20, #65535
	cmp	w0, w20
	b.eq	.LBB106_20
// %bb.17:
	bl	update_vpc_list
	adrp	x8, p_data_block_list_head
	adrp	x9, p_data_block_list_table
	ldr	w8, [x8, :lo12:p_data_block_list_head]
	ldr	w9, [x9, :lo12:p_data_block_list_table]
	adrp	x10, p_valid_page_count_table
	ldr	x10, [x10, :lo12:p_valid_page_count_table]
	strh	w20, [x19, :lo12:g_tmp_data_superblock_id]
	sub	w8, w8, w9
	mov	w9, #43691
	lsr	w8, w8, #1
	movk	w9, #43690, lsl #16
	mul	w8, w8, w9
	and	x8, x8, #0xffff
	ldrh	w9, [x10, x8, lsl #1]
	cbnz	w9, .LBB106_21
// %bb.18:
	mov	w9, #65535
	cmp	x8, x9
	b.eq	.LBB106_21
// %bb.19:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.decrement_vpc_count
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.decrement_vpc_count
	mov	w2, #2869
	bl	sftl_printk
	b	.LBB106_21
.LBB106_20:
	strh	w20, [x19, :lo12:g_tmp_data_superblock_id]
.LBB106_21:
	ldp	x20, x19, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end106:
	.size	FtlPowerLostRecovery, .Lfunc_end106-FtlPowerLostRecovery
                                        // -- End function
	.globl	FtlGcPageRecovery               // -- Begin function FtlGcPageRecovery
	.p2align	2
	.type	FtlGcPageRecovery,@function
FtlGcPageRecovery:                      // @FtlGcPageRecovery
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #16]             // 16-byte Folded Spill
	adrp	x20, c_ftl_nand_page_pre_blk
	ldrh	w1, [x20, :lo12:c_ftl_nand_page_pre_blk]
	adrp	x19, g_gc_temp_superblock
	add	x19, x19, :lo12:g_gc_temp_superblock
	mov	x0, x19
	mov	x29, sp
	bl	FtlGcScanTempBlk
	ldrh	w8, [x19, #2]
	ldrh	w9, [x20, :lo12:c_ftl_nand_page_pre_blk]
	cmp	w8, w9
	b.lo	.LBB107_7
// %bb.1:
	adrp	x8, gL2pMapInfo+6
	add	x8, x8, :lo12:gL2pMapInfo+6
	ldur	w9, [x8, #50]
	cbz	w9, .LBB107_6
// %bb.2:
	ldrh	w8, [x8]
	adrp	x9, gL2pMapInfo+40
	adrp	x10, p_sys_data_buf_1
	adrp	x12, p_sys_spare_buf
	add	x9, x9, :lo12:gL2pMapInfo+40
	ldr	x0, [x10, :lo12:p_sys_data_buf_1]
	ldr	x10, [x12, :lo12:p_sys_spare_buf]
	ldr	x13, [x9]
	adrp	x11, req_sys+4
	add	x11, x11, :lo12:req_sys+4
	sub	w19, w8, #1                     // =1
	str	wzr, [x9, #16]
	stur	x0, [x11, #4]
	stur	x10, [x11, #12]
	and	x8, x19, #0xffff
	ldr	w8, [x13, x8, lsl #2]
	str	w8, [x11]
	cbz	w8, .LBB107_4
// %bb.3:
	adrp	x0, req_sys
	add	x0, x0, :lo12:req_sys
	mov	w1, #1
	bl	FlashReadPages
	b	.LBB107_5
.LBB107_4:
	adrp	x8, c_ftl_nand_byte_pre_page
	ldrh	w2, [x8, :lo12:c_ftl_nand_byte_pre_page]
	mov	w1, #255
	bl	memset
.LBB107_5:
	adrp	x8, req_sys+8
	ldr	x2, [x8, :lo12:req_sys+8]
	adrp	x0, gL2pMapInfo
	and	w1, w19, #0xffff
	add	x0, x0, :lo12:gL2pMapInfo
	bl	FtlMapWritePage
.LBB107_6:
	mov	w0, wzr
	bl	FtlGcFreeTempBlock
	adrp	x8, ftl_gc_temp_power_lost_recovery_flag
	str	wzr, [x8, :lo12:ftl_gc_temp_power_lost_recovery_flag]
.LBB107_7:
	ldp	x20, x19, [sp, #16]             // 16-byte Folded Reload
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end107:
	.size	FtlGcPageRecovery, .Lfunc_end107-FtlGcPageRecovery
                                        // -- End function
	.globl	SupperBlkListInit               // -- Begin function SupperBlkListInit
	.p2align	2
	.type	SupperBlkListInit,@function
SupperBlkListInit:                      // @SupperBlkListInit
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-96]!           // 16-byte Folded Spill
	adrp	x8, c_ftl_nand_blk_pre_plane
	ldrh	w8, [x8, :lo12:c_ftl_nand_blk_pre_plane]
	stp	x22, x21, [sp, #64]             // 16-byte Folded Spill
	adrp	x22, p_data_block_list_table
	ldr	x0, [x22, :lo12:p_data_block_list_table]
	add	x8, x8, x8, lsl #1
	lsl	x2, x8, #1
	mov	w1, wzr
	stp	x28, x27, [sp, #16]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #32]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #48]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #80]             // 16-byte Folded Spill
	mov	x29, sp
	bl	memset
	adrp	x8, p_free_data_block_list_head
	adrp	x27, c_ftl_nand_data_blks_per_plane
	str	xzr, [x8, :lo12:p_free_data_block_list_head]
	ldrh	w8, [x27, :lo12:c_ftl_nand_data_blks_per_plane]
	adrp	x9, p_data_block_list_head
	adrp	x10, p_data_block_list_tail
	str	xzr, [x9, :lo12:p_data_block_list_head]
	str	xzr, [x10, :lo12:p_data_block_list_tail]
	adrp	x9, g_num_data_superblocks
	adrp	x10, g_num_free_superblocks
	strh	wzr, [x9, :lo12:g_num_data_superblocks]
	strh	wzr, [x10, :lo12:g_num_free_superblocks]
	cbz	w8, .LBB108_17
// %bb.1:
	adrp	x24, gBbtInfo
	mov	x19, xzr
	mov	w25, wzr
	mov	w26, wzr
	adrp	x28, c_ftl_nand_planes_num
	adrp	x23, p_valid_page_count_table
	mov	w21, #6
	adrp	x20, g_active_superblock
	add	x24, x24, :lo12:gBbtInfo
	b	.LBB108_4
.LBB108_2:                              //   in Loop: Header=BB108_4 Depth=1
	mov	w0, w19
	add	w25, w25, #1                    // =1
	bl	INSERT_FREE_LIST
.LBB108_3:                              //   in Loop: Header=BB108_4 Depth=1
	ldrh	w8, [x27, :lo12:c_ftl_nand_data_blks_per_plane]
	add	x19, x19, #1                    // =1
	cmp	x19, x8
	b.hs	.LBB108_16
.LBB108_4:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB108_6 Depth 2
	ldrh	w9, [x28, :lo12:c_ftl_nand_planes_num]
	cbz	x9, .LBB108_9
// %bb.5:                               //   in Loop: Header=BB108_4 Depth=1
	adrp	x10, c_ftl_nand_planes_per_die
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_per_die]
	adrp	x11, c_ftl_nand_blks_per_die
	adrp	x12, c_ftl_nand_page_pre_blk
	ldrh	w11, [x11, :lo12:c_ftl_nand_blks_per_die]
	ldrh	w12, [x12, :lo12:c_ftl_nand_page_pre_blk]
	adrp	x14, p_plane_order_table
	mov	w8, wzr
	mul	w13, w10, w19
	add	x14, x14, :lo12:p_plane_order_table
.LBB108_6:                              //   Parent Loop BB108_4 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldrb	w15, [x14], #1
	and	w16, w10, #0xffff
	and	w17, w15, #0xffff
	udiv	w16, w17, w16
	msub	w15, w16, w10, w15
	madd	w16, w11, w16, w13
	add	w15, w16, w15
	and	w17, w11, #0xffff
	and	w16, w15, #0xffff
	udiv	w16, w16, w17
	add	x17, x24, w16, uxtw #3
	ldr	x17, [x17, #32]
	msub	w15, w16, w11, w15
	lsr	w16, w15, #3
	and	x16, x16, #0x1ffc
	ldr	w16, [x17, x16]
	lsr	w15, w16, w15
	tst	w15, #0x1
	csel	w15, w12, wzr, eq
	subs	x9, x9, #1                      // =1
	add	w8, w15, w8
	b.ne	.LBB108_6
// %bb.7:                               //   in Loop: Header=BB108_4 Depth=1
	tst	w8, #0xffff
	b.eq	.LBB108_9
// %bb.8:                               //   in Loop: Header=BB108_4 Depth=1
	and	w8, w8, #0xffff
	mov	w9, #32768
	udiv	w8, w9, w8
	b	.LBB108_10
.LBB108_9:                              //   in Loop: Header=BB108_4 Depth=1
	ldr	x9, [x23, :lo12:p_valid_page_count_table]
	mov	w8, wzr
	mov	w10, #65535
	strh	w10, [x9, x19, lsl #1]
.LBB108_10:                             //   in Loop: Header=BB108_4 Depth=1
	ldr	x9, [x22, :lo12:p_data_block_list_table]
	madd	x9, x19, x21, x9
	strh	w8, [x9, #4]
	ldrh	w8, [x20, :lo12:g_active_superblock]
	cmp	x19, x8
	b.eq	.LBB108_3
// %bb.11:                              //   in Loop: Header=BB108_4 Depth=1
	adrp	x8, g_buffer_superblock
	ldrh	w8, [x8, :lo12:g_buffer_superblock]
	cmp	x19, x8
	b.eq	.LBB108_3
// %bb.12:                              //   in Loop: Header=BB108_4 Depth=1
	adrp	x8, g_gc_temp_superblock
	ldrh	w8, [x8, :lo12:g_gc_temp_superblock]
	cmp	x19, x8
	b.eq	.LBB108_3
// %bb.13:                              //   in Loop: Header=BB108_4 Depth=1
	ldr	x8, [x23, :lo12:p_valid_page_count_table]
	ldrh	w8, [x8, x19, lsl #1]
	cbz	w8, .LBB108_2
// %bb.14:                              //   in Loop: Header=BB108_4 Depth=1
	mov	w0, w19
	add	w26, w26, #1                    // =1
	bl	insert_data_list
	adrp	x10, g_num_data_superblocks
	ldrh	w8, [x10, :lo12:g_num_data_superblocks]
	ldrh	w9, [x27, :lo12:c_ftl_nand_data_blks_per_plane]
	add	w8, w8, #1                      // =1
	cmp	w9, w8, uxth
	strh	w8, [x10, :lo12:g_num_data_superblocks]
	b.hs	.LBB108_3
// %bb.15:                              //   in Loop: Header=BB108_4 Depth=1
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.INSERT_DATA_LIST
	mov	w2, #214
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.INSERT_DATA_LIST
	bl	sftl_printk
	b	.LBB108_3
.LBB108_16:
	adrp	x10, g_num_free_superblocks
	b	.LBB108_18
.LBB108_17:
	mov	w26, wzr
	mov	w25, wzr
.LBB108_18:
	adrp	x9, g_num_data_superblocks
	strh	w26, [x9, :lo12:g_num_data_superblocks]
	and	w9, w25, #0xffff
	add	w9, w9, w26, uxth
	cmp	w9, w8
	strh	w25, [x10, :lo12:g_num_free_superblocks]
	b.ls	.LBB108_20
// %bb.19:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.SupperBlkListInit
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.SupperBlkListInit
	mov	w2, #2219
	bl	sftl_printk
.LBB108_20:
	ldp	x20, x19, [sp, #80]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #64]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #48]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #32]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #96             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end108:
	.size	SupperBlkListInit, .Lfunc_end108-SupperBlkListInit
                                        // -- End function
	.globl	Ftl_load_ext_data               // -- Begin function Ftl_load_ext_data
	.p2align	2
	.type	Ftl_load_ext_data,@function
Ftl_load_ext_data:                      // @Ftl_load_ext_data
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #16]             // 16-byte Folded Spill
	adrp	x19, g_sys_ext_data
	add	x19, x19, :lo12:g_sys_ext_data
	mov	w20, #19539
	mov	w1, #1
	mov	w0, wzr
	mov	x2, x19
	mov	x29, sp
	movk	w20, #18004, lsl #16
	bl	FtlVendorPartRead
	ldr	w8, [x19]
	cmp	w8, w20
	b.ne	.LBB109_2
// %bb.1:
	adrp	x12, g_sys_ext_data+8
	add	x12, x12, :lo12:g_sys_ext_data+8
	ldp	w0, w18, [x12, #80]
	ldp	w17, w16, [x12]
	ldp	w15, w14, [x12, #8]
	ldp	w8, w13, [x12, #20]
	ldp	w9, w11, [x12, #28]
	ldp	w10, w12, [x12, #36]
	b	.LBB109_3
.LBB109_2:
	add	x0, x19, #4                     // =4
	mov	w2, #508
	mov	w1, wzr
	bl	memset
	mov	w12, wzr
	mov	w10, wzr
	mov	w11, wzr
	mov	w9, wzr
	mov	w13, wzr
	mov	w8, wzr
	mov	w14, wzr
	mov	w15, wzr
	mov	w16, wzr
	mov	w17, wzr
	mov	w18, wzr
	mov	w0, wzr
	str	w20, [x19]
.LBB109_3:
	adrp	x1, g_totle_write_sector
	str	w0, [x1, :lo12:g_totle_write_sector]
	adrp	x0, g_totle_read_sector
	adrp	x1, g_totle_gc_page_count
	str	w18, [x0, :lo12:g_totle_read_sector]
	adrp	x18, g_totle_write_page_count
	adrp	x0, g_totle_read_page_count
	str	w17, [x1, :lo12:g_totle_gc_page_count]
	adrp	x17, g_totle_l2p_write_count
	str	w16, [x18, :lo12:g_totle_write_page_count]
	adrp	x16, g_totle_sys_slc_erase_count
	adrp	x18, g_totle_discard_page_count
	str	w15, [x0, :lo12:g_totle_read_page_count]
	adrp	x15, g_totle_cache_write_count
	str	w14, [x17, :lo12:g_totle_l2p_write_count]
	adrp	x17, g_totle_mlc_erase_count
	str	w13, [x16, :lo12:g_totle_sys_slc_erase_count]
	adrp	x13, c_mlc_erase_count_value
	adrp	x16, c_ftl_nand_data_blks_per_plane
	str	w9, [x18, :lo12:g_totle_discard_page_count]
	str	w11, [x15, :lo12:g_totle_cache_write_count]
	ldr	w9, [x17, :lo12:g_totle_mlc_erase_count]
	ldrh	w11, [x13, :lo12:c_mlc_erase_count_value]
	ldrh	w13, [x16, :lo12:c_ftl_nand_data_blks_per_plane]
	adrp	x1, g_totle_slc_erase_count
	ldp	x20, x19, [sp, #16]             // 16-byte Folded Reload
	str	w8, [x1, :lo12:g_totle_slc_erase_count]
	madd	w8, w9, w11, w8
	adrp	x0, g_max_erase_count
	adrp	x14, g_min_erase_count
	udiv	w8, w8, w13
	adrp	x9, g_totle_avg_erase_count
	str	w10, [x0, :lo12:g_max_erase_count]
	str	w12, [x14, :lo12:g_min_erase_count]
	str	w8, [x9, :lo12:g_totle_avg_erase_count]
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end109:
	.size	Ftl_load_ext_data, .Lfunc_end109-Ftl_load_ext_data
                                        // -- End function
	.globl	FtlSuperblockPowerLostFix       // -- Begin function FtlSuperblockPowerLostFix
	.p2align	2
	.type	FtlSuperblockPowerLostFix,@function
FtlSuperblockPowerLostFix:              // @FtlSuperblockPowerLostFix
// %bb.0:
	hint	#25
	sub	sp, sp, #96                     // =96
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	stp	x29, x30, [sp, #48]             // 16-byte Folded Spill
	add	x29, sp, #48                    // =48
	stp	x22, x21, [sp, #64]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #80]             // 16-byte Folded Spill
	adrp	x10, p_sys_data_buf
	adrp	x11, p_sys_spare_buf
	stur	x8, [x29, #-8]
	ldr	x8, [x10, :lo12:p_sys_data_buf]
	ldr	x20, [x11, :lo12:p_sys_spare_buf]
	mov	w9, #-1
	str	w9, [sp, #32]
	mov	x9, #-3
	movk	x9, #65534, lsl #32
	stp	x8, x20, [sp, #16]
	str	x9, [x20, #8]
	ldrh	w8, [x0]
	strh	wzr, [x20]
	mov	w9, #61589
	mov	x19, x0
	strh	w8, [x20, #2]
	ldr	x8, [x10, :lo12:p_sys_data_buf]
	str	w9, [x8]
	ldr	x8, [x10, :lo12:p_sys_data_buf]
	mov	w9, #22136
	movk	w9, #4660, lsl #16
	str	w9, [x8, #4]
	ldrh	w8, [x0, #4]
	cbz	w8, .LBB110_5
// %bb.1:
	tst	w8, #0x1
	mov	w8, #6
	cinc	w21, w8, ne
	adrp	x22, g_GlobalDataVersion
.LBB110_2:                              // =>This Inner Loop Header: Depth=1
	mov	x0, x19
	bl	get_new_active_ppa
	cmn	w0, #1                          // =1
	str	w0, [sp, #12]
	b.eq	.LBB110_5
// %bb.3:                               //   in Loop: Header=BB110_2 Depth=1
	ldr	w8, [x22, :lo12:g_GlobalDataVersion]
	add	x0, sp, #8                      // =8
	mov	w1, #1
	mov	w2, wzr
	add	w9, w8, #1                      // =1
	cmn	w9, #1                          // =1
	str	w8, [x20, #4]
	csinc	w8, wzr, w8, eq
	str	w8, [x22, :lo12:g_GlobalDataVersion]
	bl	FlashProgPages
	ldrh	w0, [x19]
	bl	decrement_vpc_count
	subs	w21, w21, #1                    // =1
	b.eq	.LBB110_5
// %bb.4:                               //   in Loop: Header=BB110_2 Depth=1
	ldrh	w8, [x19, #4]
	cbnz	w8, .LBB110_2
.LBB110_5:
	adrp	x8, p_valid_page_count_table
	ldrh	w9, [x19]
	ldr	x8, [x8, :lo12:p_valid_page_count_table]
	ldrh	w10, [x19, #4]
	lsl	x9, x9, #1
	ldrh	w11, [x8, x9]
	sub	w10, w11, w10
	adrp	x11, c_ftl_nand_page_pre_blk
	strh	w10, [x8, x9]
	ldrh	w8, [x11, :lo12:c_ftl_nand_page_pre_blk]
	strb	wzr, [x19, #6]
	strh	wzr, [x19, #4]
	adrp	x9, __stack_chk_guard
	strh	w8, [x19, #2]
	ldur	x8, [x29, #-8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB110_7
// %bb.6:
	ldp	x20, x19, [sp, #80]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #64]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #48]             // 16-byte Folded Reload
	add	sp, sp, #96                     // =96
	hint	#29
	ret
.LBB110_7:
	bl	__stack_chk_fail
.Lfunc_end110:
	.size	FtlSuperblockPowerLostFix, .Lfunc_end110-FtlSuperblockPowerLostFix
                                        // -- End function
	.globl	FtlVpcCheckAndModify            // -- Begin function FtlVpcCheckAndModify
	.p2align	2
	.type	FtlVpcCheckAndModify,@function
FtlVpcCheckAndModify:                   // @FtlVpcCheckAndModify
// %bb.0:
	hint	#25
	sub	sp, sp, #112                    // =112
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	adrp	x0, .L.str.102
	adrp	x1, .L__func__.FtlVpcCheckAndModify
	add	x0, x0, :lo12:.L.str.102
	add	x1, x1, :lo12:.L__func__.FtlVpcCheckAndModify
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	stp	x28, x27, [sp, #32]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #48]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #64]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #80]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #96]             // 16-byte Folded Spill
	add	x29, sp, #16                    // =16
	str	x8, [sp, #8]
	bl	sftl_printk
	adrp	x8, c_ftl_nand_blk_pre_plane
	adrp	x25, p_valid_page_count_check_table
	ldrh	w8, [x8, :lo12:c_ftl_nand_blk_pre_plane]
	ldr	x0, [x25, :lo12:p_valid_page_count_check_table]
	mov	w1, wzr
	lsl	x2, x8, #1
	bl	memset
	adrp	x20, g_MaxLpn
	ldr	w8, [x20, :lo12:g_MaxLpn]
	cbz	w8, .LBB111_5
// %bb.1:
	mov	w19, wzr
	adrp	x21, c_ftl_nand_blks_per_die
	adrp	x22, c_ftl_nand_planes_per_die
	b	.LBB111_3
.LBB111_2:                              //   in Loop: Header=BB111_3 Depth=1
	ldr	w8, [x20, :lo12:g_MaxLpn]
	add	w19, w19, #1                    // =1
	cmp	w19, w8
	b.hs	.LBB111_5
.LBB111_3:                              // =>This Inner Loop Header: Depth=1
	add	x1, sp, #4                      // =4
	mov	w0, w19
	mov	w2, wzr
	bl	log2phys
	ldr	w8, [sp, #4]
	cmn	w8, #1                          // =1
	b.eq	.LBB111_2
// %bb.4:                               //   in Loop: Header=BB111_3 Depth=1
	ldrh	w9, [x21, :lo12:c_ftl_nand_blks_per_die]
	ldrh	w10, [x22, :lo12:c_ftl_nand_planes_per_die]
	ubfx	w8, w8, #10, #16
	ldr	x12, [x25, :lo12:p_valid_page_count_check_table]
	udiv	w11, w8, w9
	msub	w8, w11, w9, w8
	udiv	w8, w8, w10
	lsl	x8, x8, #1
	ldrh	w9, [x12, x8]
	add	w9, w9, #1                      // =1
	strh	w9, [x12, x8]
	b	.LBB111_2
.LBB111_5:
	adrp	x26, c_ftl_nand_data_blks_per_plane
	ldrh	w8, [x26, :lo12:c_ftl_nand_data_blks_per_plane]
	cbz	w8, .LBB111_30
// %bb.6:
	adrp	x24, .L.str.103
	mov	x27, xzr
	mov	x19, xzr
	adrp	x28, p_valid_page_count_table
	mov	w22, #65535
	add	x24, x24, :lo12:.L.str.103
	adrp	x20, g_active_superblock
	adrp	x23, g_gc_temp_superblock
	b	.LBB111_9
.LBB111_7:                              //   in Loop: Header=BB111_9 Depth=1
	mov	w0, w19
	bl	update_vpc_list
.LBB111_8:                              //   in Loop: Header=BB111_9 Depth=1
	ldrh	w8, [x26, :lo12:c_ftl_nand_data_blks_per_plane]
	add	x19, x19, #1                    // =1
	add	x27, x27, #6                    // =6
	cmp	x19, x8
	b.hs	.LBB111_30
.LBB111_9:                              // =>This Inner Loop Header: Depth=1
	ldr	x8, [x28, :lo12:p_valid_page_count_table]
	lsl	x21, x19, #1
	ldrh	w2, [x8, x21]
	cmp	w2, w22
	b.eq	.LBB111_8
// %bb.10:                              //   in Loop: Header=BB111_9 Depth=1
	ldr	x8, [x25, :lo12:p_valid_page_count_check_table]
	ldrh	w3, [x8, x21]
	cmp	w2, w3
	b.eq	.LBB111_8
// %bb.11:                              //   in Loop: Header=BB111_9 Depth=1
	mov	x0, x24
	mov	w1, w19
	bl	sftl_printk
	ldrh	w8, [x20, :lo12:g_active_superblock]
	cmp	x19, x8
	b.eq	.LBB111_8
// %bb.12:                              //   in Loop: Header=BB111_9 Depth=1
	ldrh	w8, [x23, :lo12:g_gc_temp_superblock]
	cmp	x19, x8
	b.eq	.LBB111_8
// %bb.13:                              //   in Loop: Header=BB111_9 Depth=1
	adrp	x8, g_buffer_superblock
	ldrh	w8, [x8, :lo12:g_buffer_superblock]
	cmp	x19, x8
	b.eq	.LBB111_8
// %bb.14:                              //   in Loop: Header=BB111_9 Depth=1
	ldr	x8, [x28, :lo12:p_valid_page_count_table]
	ldr	x9, [x25, :lo12:p_valid_page_count_check_table]
	ldrh	w10, [x8, x21]
	ldrh	w9, [x9, x21]
	strh	w9, [x8, x21]
	cbnz	w10, .LBB111_7
// %bb.15:                              //   in Loop: Header=BB111_9 Depth=1
	adrp	x8, p_data_block_list_table
	ldr	x21, [x8, :lo12:p_data_block_list_table]
	adrp	x9, p_free_data_block_list_head
	ldr	x9, [x9, :lo12:p_free_data_block_list_head]
	add	x24, x21, x27
	ldrsh	w8, [x24, #2]
	cmn	w8, #1                          // =1
	b.ne	.LBB111_18
// %bb.16:                              //   in Loop: Header=BB111_9 Depth=1
	cmp	x24, x9
	b.eq	.LBB111_18
// %bb.17:                              //   in Loop: Header=BB111_9 Depth=1
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.List_remove_node
	mov	w2, #372
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.List_remove_node
	bl	sftl_printk
	adrp	x8, p_free_data_block_list_head
	ldr	x9, [x8, :lo12:p_free_data_block_list_head]
.LBB111_18:                             //   in Loop: Header=BB111_9 Depth=1
	ldrh	w8, [x21, x27]
	cmp	x24, x9
	b.eq	.LBB111_21
// %bb.19:                              //   in Loop: Header=BB111_9 Depth=1
	cmp	w8, w22
	b.eq	.LBB111_23
// %bb.20:                              //   in Loop: Header=BB111_9 Depth=1
	adrp	x11, p_data_block_list_table
	ldr	x9, [x11, :lo12:p_data_block_list_table]
	ldrh	w10, [x24, #2]
	mov	w12, #6
	madd	x8, x8, x12, x9
	strh	w10, [x8, #2]
	ldrh	w9, [x24, #2]
	ldr	x8, [x11, :lo12:p_data_block_list_table]
	ldrh	w10, [x21, x27]
	mul	x9, x9, x12
	strh	w10, [x8, x9]
	b	.LBB111_26
.LBB111_21:                             //   in Loop: Header=BB111_9 Depth=1
	cmp	w8, w22
	b.eq	.LBB111_25
// %bb.22:                              //   in Loop: Header=BB111_9 Depth=1
	adrp	x9, p_data_block_list_table
	ldr	x9, [x9, :lo12:p_data_block_list_table]
	mov	w10, #6
	madd	x8, x8, x10, x9
	adrp	x9, p_free_data_block_list_head
	str	x8, [x9, :lo12:p_free_data_block_list_head]
	strh	w22, [x8, #2]
	b	.LBB111_26
.LBB111_23:                             //   in Loop: Header=BB111_9 Depth=1
	ldrh	w8, [x24, #2]
	cmp	x8, x22
	b.eq	.LBB111_26
// %bb.24:                              //   in Loop: Header=BB111_9 Depth=1
	adrp	x9, p_data_block_list_table
	ldr	x9, [x9, :lo12:p_data_block_list_table]
	mov	w10, #6
	mul	x8, x8, x10
	strh	w22, [x9, x8]
	b	.LBB111_26
.LBB111_25:                             //   in Loop: Header=BB111_9 Depth=1
	adrp	x8, p_free_data_block_list_head
	str	xzr, [x8, :lo12:p_free_data_block_list_head]
.LBB111_26:                             //   in Loop: Header=BB111_9 Depth=1
	strh	w22, [x21, x27]
	strh	w22, [x24, #2]
	adrp	x21, g_num_free_superblocks
	ldrh	w8, [x21, :lo12:g_num_free_superblocks]
	cbnz	w8, .LBB111_28
// %bb.27:                              //   in Loop: Header=BB111_9 Depth=1
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlVpcCheckAndModify
	mov	w2, #2330
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlVpcCheckAndModify
	bl	sftl_printk
	ldrh	w8, [x21, :lo12:g_num_free_superblocks]
.LBB111_28:                             //   in Loop: Header=BB111_9 Depth=1
	sub	w8, w8, #1                      // =1
	mov	w0, w19
	strh	w8, [x21, :lo12:g_num_free_superblocks]
	bl	insert_data_list
	adrp	x10, g_num_data_superblocks
	ldrh	w8, [x10, :lo12:g_num_data_superblocks]
	ldrh	w9, [x26, :lo12:c_ftl_nand_data_blks_per_plane]
	adrp	x24, .L.str.103
	add	x24, x24, :lo12:.L.str.103
	add	w8, w8, #1                      // =1
	cmp	w9, w8, uxth
	strh	w8, [x10, :lo12:g_num_data_superblocks]
	b.hs	.LBB111_8
// %bb.29:                              //   in Loop: Header=BB111_9 Depth=1
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.INSERT_DATA_LIST
	mov	w2, #214
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.INSERT_DATA_LIST
	bl	sftl_printk
	b	.LBB111_8
.LBB111_30:
	bl	FtlWriteDump_data
	adrp	x20, c_ftl_nand_l2pmap_ram_region_num
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	cbz	w8, .LBB111_35
// %bb.31:
	adrp	x19, gL2pMapInfo
	mov	x21, xzr
	mov	x22, xzr
	adrp	x23, p_l2p_ram_map
	add	x19, x19, :lo12:gL2pMapInfo
	b	.LBB111_33
.LBB111_32:                             //   in Loop: Header=BB111_33 Depth=1
	add	x22, x22, #1                    // =1
	cmp	x22, w8, uxth
	add	x21, x21, #16                   // =16
	b.hs	.LBB111_35
.LBB111_33:                             // =>This Inner Loop Header: Depth=1
	ldr	x9, [x23, :lo12:p_l2p_ram_map]
	add	x9, x9, x21
	ldr	w10, [x9, #4]
	tbz	w10, #31, .LBB111_32
// %bb.34:                              //   in Loop: Header=BB111_33 Depth=1
	ldrh	w1, [x9]
	ldr	x2, [x9, #8]
	mov	x0, x19
	bl	FtlMapWritePage
	ldr	x8, [x23, :lo12:p_l2p_ram_map]
	add	x8, x8, x21
	ldr	w9, [x8, #4]
	and	w9, w9, #0x7fffffff
	str	w9, [x8, #4]
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	b	.LBB111_32
.LBB111_35:
	bl	FtlVpcTblFlush
	adrp	x9, __stack_chk_guard
	ldr	x8, [sp, #8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB111_37
// %bb.36:
	ldp	x20, x19, [sp, #96]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #80]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #64]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #48]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	add	sp, sp, #112                    // =112
	hint	#29
	ret
.LBB111_37:
	bl	__stack_chk_fail
.Lfunc_end111:
	.size	FtlVpcCheckAndModify, .Lfunc_end111-FtlVpcCheckAndModify
                                        // -- End function
	.globl	update_vpc_list                 // -- Begin function update_vpc_list
	.p2align	2
	.type	update_vpc_list,@function
update_vpc_list:                        // @update_vpc_list
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-80]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #64]             // 16-byte Folded Spill
	adrp	x20, p_valid_page_count_table
	ldr	x8, [x20, :lo12:p_valid_page_count_table]
	mov	w19, w0
	stp	x22, x21, [sp, #48]             // 16-byte Folded Spill
	and	x21, x19, #0xffff
	ldrh	w8, [x8, x21, lsl #1]
	str	x25, [sp, #16]                  // 8-byte Folded Spill
	stp	x24, x23, [sp, #32]             // 16-byte Folded Spill
	mov	x29, sp
	cbz	w8, .LBB112_2
// %bb.1:
	mov	w0, w19
	bl	List_update_data_list
	mov	w0, wzr
	b	.LBB112_29
.LBB112_2:
	adrp	x8, g_gc_next_blk
	ldrh	w9, [x8, :lo12:g_gc_next_blk]
	cmp	w9, w19, uxth
	b.ne	.LBB112_4
// %bb.3:
	mov	w9, #65535
	strh	w9, [x8, :lo12:g_gc_next_blk]
.LBB112_4:
	adrp	x8, g_gc_next_blk_1
	ldrh	w9, [x8, :lo12:g_gc_next_blk_1]
	cmp	w9, w19, uxth
	b.ne	.LBB112_6
// %bb.5:
	mov	w9, #65535
	strh	w9, [x8, :lo12:g_gc_next_blk_1]
.LBB112_6:
	adrp	x8, g_gc_superblock
	ldrh	w9, [x8, :lo12:g_gc_superblock]
	cmp	w9, w19, uxth
	b.ne	.LBB112_8
// %bb.7:
	mov	w9, #65535
	strh	w9, [x8, :lo12:g_gc_superblock]
	b	.LBB112_11
.LBB112_8:
	adrp	x8, g_active_superblock
	ldrh	w8, [x8, :lo12:g_active_superblock]
	mov	w0, wzr
	cmp	w8, w19, uxth
	b.eq	.LBB112_29
// %bb.9:
	adrp	x8, g_buffer_superblock
	ldrh	w8, [x8, :lo12:g_buffer_superblock]
	cmp	w8, w19, uxth
	b.eq	.LBB112_29
// %bb.10:
	adrp	x8, g_gc_temp_superblock
	ldrh	w8, [x8, :lo12:g_gc_temp_superblock]
	cmp	w8, w19, uxth
	b.eq	.LBB112_29
.LBB112_11:
	adrp	x24, p_data_block_list_table
	ldr	x8, [x24, :lo12:p_data_block_list_table]
	mov	w9, #6
	adrp	x25, p_data_block_list_head
	madd	x22, x21, x9, x8
	mov	x23, x22
	ldrsh	w8, [x23, #2]!
	ldr	x9, [x25, :lo12:p_data_block_list_head]
	cmn	w8, #1                          // =1
	b.ne	.LBB112_14
// %bb.12:
	cmp	x22, x9
	b.eq	.LBB112_14
// %bb.13:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.List_remove_node
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.List_remove_node
	mov	w2, #372
	bl	sftl_printk
	ldr	x9, [x25, :lo12:p_data_block_list_head]
.LBB112_14:
	ldrh	w8, [x22]
	cmp	x22, x9
	b.eq	.LBB112_17
// %bb.15:
	mov	w9, #65535
	cmp	w8, w9
	b.eq	.LBB112_19
// %bb.16:
	ldr	x9, [x24, :lo12:p_data_block_list_table]
	ldrh	w10, [x23]
	mov	w11, #6
	madd	x8, x8, x11, x9
	strh	w10, [x8, #2]
	ldrh	w9, [x23]
	ldr	x8, [x24, :lo12:p_data_block_list_table]
	ldrh	w10, [x22]
	mul	x9, x9, x11
	strh	w10, [x8, x9]
	b	.LBB112_22
.LBB112_17:
	mov	w9, #65535
	cmp	w8, w9
	b.eq	.LBB112_21
// %bb.18:
	ldr	x10, [x24, :lo12:p_data_block_list_table]
	mov	w11, #6
	madd	x8, x8, x11, x10
	str	x8, [x25, :lo12:p_data_block_list_head]
	strh	w9, [x8, #2]
	b	.LBB112_22
.LBB112_19:
	ldrh	w8, [x23]
	mov	w9, #65535
	cmp	x8, x9
	b.eq	.LBB112_22
// %bb.20:
	ldr	x10, [x24, :lo12:p_data_block_list_table]
	mov	w11, #6
	mul	x8, x8, x11
	strh	w9, [x10, x8]
	b	.LBB112_22
.LBB112_21:
	str	xzr, [x25, :lo12:p_data_block_list_head]
.LBB112_22:
	mov	w8, #65535
	strh	w8, [x22]
	strh	w8, [x23]
	adrp	x22, g_num_data_superblocks
	ldrh	w8, [x22, :lo12:g_num_data_superblocks]
	cbnz	w8, .LBB112_24
// %bb.23:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.update_vpc_list
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.update_vpc_list
	mov	w2, #2835
	bl	sftl_printk
	ldrh	w8, [x22, :lo12:g_num_data_superblocks]
.LBB112_24:
	mvn	w9, w19
	sub	w8, w8, #1                      // =1
	tst	w9, #0xffff
	strh	w8, [x22, :lo12:g_num_data_superblocks]
	b.eq	.LBB112_26
// %bb.25:
	ldr	x8, [x20, :lo12:p_valid_page_count_table]
	mov	w0, w19
	strh	wzr, [x8, x21, lsl #1]
	bl	INSERT_FREE_LIST
.LBB112_26:
	mov	w0, w19
	bl	FtlGcFreeBadSuperBlk
	adrp	x8, g_num_free_superblocks
	ldrh	w9, [x22, :lo12:g_num_data_superblocks]
	ldrh	w8, [x8, :lo12:g_num_free_superblocks]
	adrp	x10, c_ftl_nand_data_blks_per_plane
	ldrh	w10, [x10, :lo12:c_ftl_nand_data_blks_per_plane]
	add	w8, w9, w8
	cmp	w8, w10
	b.ls	.LBB112_28
// %bb.27:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.update_vpc_list
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.update_vpc_list
	mov	w2, #2838
	bl	sftl_printk
.LBB112_28:
	mov	w0, #1
.LBB112_29:
	ldp	x20, x19, [sp, #64]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #48]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #32]             // 16-byte Folded Reload
	ldr	x25, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #80             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end112:
	.size	update_vpc_list, .Lfunc_end112-update_vpc_list
                                        // -- End function
	.globl	ftl_check_vpc                   // -- Begin function ftl_check_vpc
	.p2align	2
	.type	ftl_check_vpc,@function
ftl_check_vpc:                          // @ftl_check_vpc
// %bb.0:
	hint	#25
	sub	sp, sp, #112                    // =112
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	adrp	x0, .L.str.102
	adrp	x1, .L__func__.ftl_check_vpc
	add	x0, x0, :lo12:.L.str.102
	add	x1, x1, :lo12:.L__func__.ftl_check_vpc
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	stp	x28, x27, [sp, #32]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #48]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #64]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #80]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #96]             // 16-byte Folded Spill
	add	x29, sp, #16                    // =16
	str	x8, [sp, #8]
	bl	sftl_printk
	adrp	x19, check_vpc_table
	add	x19, x19, :lo12:check_vpc_table
	mov	w2, #8192
	mov	x0, x19
	mov	w1, wzr
	bl	memset
	adrp	x21, g_MaxLpn
	ldr	w8, [x21, :lo12:g_MaxLpn]
	cbz	w8, .LBB113_5
// %bb.1:
	mov	w20, wzr
	adrp	x22, c_ftl_nand_blks_per_die
	adrp	x23, c_ftl_nand_planes_per_die
	b	.LBB113_3
.LBB113_2:                              //   in Loop: Header=BB113_3 Depth=1
	ldr	w8, [x21, :lo12:g_MaxLpn]
	add	w20, w20, #1                    // =1
	cmp	w20, w8
	b.hs	.LBB113_5
.LBB113_3:                              // =>This Inner Loop Header: Depth=1
	add	x1, sp, #4                      // =4
	mov	w0, w20
	mov	w2, wzr
	bl	log2phys
	ldr	w8, [sp, #4]
	cmn	w8, #1                          // =1
	b.eq	.LBB113_2
// %bb.4:                               //   in Loop: Header=BB113_3 Depth=1
	ldrh	w9, [x22, :lo12:c_ftl_nand_blks_per_die]
	ldrh	w10, [x23, :lo12:c_ftl_nand_planes_per_die]
	ubfx	w8, w8, #10, #16
	udiv	w11, w8, w9
	msub	w8, w11, w9, w8
	udiv	w8, w8, w10
	lsl	x8, x8, #1
	ldrh	w9, [x19, x8]
	add	w9, w9, #1                      // =1
	strh	w9, [x19, x8]
	b	.LBB113_2
.LBB113_5:
	adrp	x24, c_ftl_nand_data_blks_per_plane
	ldrh	w8, [x24, :lo12:c_ftl_nand_data_blks_per_plane]
	adrp	x22, p_valid_page_count_table
	cbz	w8, .LBB113_11
// %bb.6:
	ldr	x8, [x22, :lo12:p_valid_page_count_table]
	adrp	x21, .L.str.104
	mov	x20, xzr
	mov	w23, wzr
	add	x21, x21, :lo12:.L.str.104
	mov	w25, #65535
	b	.LBB113_8
.LBB113_7:                              //   in Loop: Header=BB113_8 Depth=1
	ldrh	w9, [x24, :lo12:c_ftl_nand_data_blks_per_plane]
	add	x20, x20, #1                    // =1
	cmp	x20, x9
	b.hs	.LBB113_12
.LBB113_8:                              // =>This Inner Loop Header: Depth=1
	lsl	x9, x20, #1
	ldrh	w2, [x8, x9]
	ldrh	w3, [x19, x9]
	cmp	w2, w3
	b.eq	.LBB113_7
// %bb.9:                               //   in Loop: Header=BB113_8 Depth=1
	mov	x0, x21
	mov	w1, w20
	bl	sftl_printk
	ldr	x8, [x22, :lo12:p_valid_page_count_table]
	ldrh	w9, [x8, x20, lsl #1]
	cmp	w9, w25
	b.eq	.LBB113_7
// %bb.10:                              //   in Loop: Header=BB113_8 Depth=1
	ldrh	w10, [x19, x20, lsl #1]
	cmp	w9, w10
	csinc	w23, w23, wzr, hs
	b	.LBB113_7
.LBB113_11:
	mov	w23, wzr
.LBB113_12:
	adrp	x8, p_free_data_block_list_head
	ldr	x9, [x8, :lo12:p_free_data_block_list_head]
	cbz	x9, .LBB113_19
// %bb.13:
	adrp	x8, g_num_free_superblocks
	ldrh	w24, [x8, :lo12:g_num_free_superblocks]
	cbz	w24, .LBB113_19
// %bb.14:
	adrp	x26, p_data_block_list_table
	ldr	x8, [x26, :lo12:p_data_block_list_table]
	mov	w10, #43691
	movk	w10, #43690, lsl #16
	adrp	x20, .L.str.105
	sub	x9, x9, x8
	lsr	x9, x9, #1
	mov	w25, wzr
	mov	w27, #6
	mov	w28, #65535
	mul	w9, w9, w10
	add	x20, x20, :lo12:.L.str.105
.LBB113_15:                             // =>This Inner Loop Header: Depth=1
	ldr	x10, [x22, :lo12:p_valid_page_count_table]
	and	x21, x9, #0xffff
	ldrh	w2, [x10, x21, lsl #1]
	cbz	w2, .LBB113_17
// %bb.16:                              //   in Loop: Header=BB113_15 Depth=1
	ldrh	w3, [x19, x21, lsl #1]
	mov	x0, x20
	mov	w1, w21
	bl	sftl_printk
	ldr	x8, [x26, :lo12:p_data_block_list_table]
	mov	w23, #1
.LBB113_17:                             //   in Loop: Header=BB113_15 Depth=1
	mul	x9, x21, x27
	ldrh	w9, [x8, x9]
	cmp	w9, w28
	b.eq	.LBB113_19
// %bb.18:                              //   in Loop: Header=BB113_15 Depth=1
	add	w25, w25, #1                    // =1
	cmp	w24, w25, uxth
	b.hi	.LBB113_15
.LBB113_19:
	tst	w23, #0xffff
	b.eq	.LBB113_21
// %bb.20:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.ftl_check_vpc
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.ftl_check_vpc
	mov	w2, #2394
	bl	sftl_printk
.LBB113_21:
	adrp	x9, __stack_chk_guard
	ldr	x8, [sp, #8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB113_23
// %bb.22:
	ldp	x20, x19, [sp, #96]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #80]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #64]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #48]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	add	sp, sp, #112                    // =112
	hint	#29
	ret
.LBB113_23:
	bl	__stack_chk_fail
.Lfunc_end113:
	.size	ftl_check_vpc, .Lfunc_end113-ftl_check_vpc
                                        // -- End function
	.globl	ftl_scan_all_data               // -- Begin function ftl_scan_all_data
	.p2align	2
	.type	ftl_scan_all_data,@function
ftl_scan_all_data:                      // @ftl_scan_all_data
// %bb.0:
	hint	#25
	sub	sp, sp, #128                    // =128
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	adrp	x0, .L.str.106
	stp	x29, x30, [sp, #32]             // 16-byte Folded Spill
	add	x29, sp, #32                    // =32
	add	x0, x0, :lo12:.L.str.106
	mov	w1, wzr
	stp	x28, x27, [sp, #48]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #64]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #80]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #96]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #112]            // 16-byte Folded Spill
	stur	x8, [x29, #-8]
	bl	sftl_printk
	adrp	x24, g_MaxLpn
	ldr	w8, [x24, :lo12:g_MaxLpn]
	cbz	w8, .LBB114_10
// %bb.1:
	adrp	x20, .L.str.107
	adrp	x21, req_sys
	adrp	x27, req_sys+4
	adrp	x22, .L.str.108
	mov	w19, wzr
	add	x20, x20, :lo12:.L.str.107
	add	x21, x21, :lo12:req_sys
	adrp	x25, p_sys_data_buf
	adrp	x26, p_sys_spare_buf
	add	x27, x27, :lo12:req_sys+4
	add	x22, x22, :lo12:.L.str.108
	b	.LBB114_4
.LBB114_2:                              //   in Loop: Header=BB114_4 Depth=1
	ldur	x8, [x27, #12]
	ldur	x9, [x27, #4]
	ldr	w2, [x27]
	mov	x0, x22
	ldp	w3, w4, [x8]
	ldp	w5, w6, [x8, #8]
	ldp	w7, w8, [x9]
	mov	w1, w19
	str	w8, [sp]
	bl	sftl_printk
.LBB114_3:                              //   in Loop: Header=BB114_4 Depth=1
	ldr	w8, [x24, :lo12:g_MaxLpn]
	add	w19, w19, #1                    // =1
	cmp	w19, w8
	b.hs	.LBB114_10
.LBB114_4:                              // =>This Inner Loop Header: Depth=1
	sub	x1, x29, #12                    // =12
	mov	w0, w19
	mov	w2, wzr
	bl	log2phys
	ldur	w23, [x29, #-12]
	tst	w19, #0x7ff
	b.eq	.LBB114_6
// %bb.5:                               //   in Loop: Header=BB114_4 Depth=1
	cmn	w23, #1                         // =1
	b.ne	.LBB114_7
	b	.LBB114_3
.LBB114_6:                              //   in Loop: Header=BB114_4 Depth=1
	mov	x0, x20
	mov	w1, w19
	mov	w2, w23
	bl	sftl_printk
	cmn	w23, #1                         // =1
	b.eq	.LBB114_3
.LBB114_7:                              //   in Loop: Header=BB114_4 Depth=1
	ldr	x8, [x25, :lo12:p_sys_data_buf]
	ldr	x28, [x26, :lo12:p_sys_spare_buf]
	mov	w1, #1
	mov	x0, x21
	str	w19, [x21, #24]
	stp	x8, x28, [x21, #8]
	stp	wzr, w23, [x21]
	bl	FlashReadPages
	ldr	w8, [x21]
	cmn	w8, #1                          // =1
	b.eq	.LBB114_2
// %bb.8:                               //   in Loop: Header=BB114_4 Depth=1
	cmp	w8, #256                        // =256
	b.eq	.LBB114_2
// %bb.9:                               //   in Loop: Header=BB114_4 Depth=1
	ldr	w8, [x28, #8]
	cmp	w19, w8
	b.ne	.LBB114_2
	b	.LBB114_3
.LBB114_10:
	adrp	x9, __stack_chk_guard
	ldur	x8, [x29, #-8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB114_12
// %bb.11:
	ldp	x20, x19, [sp, #112]            // 16-byte Folded Reload
	ldp	x22, x21, [sp, #96]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #80]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #64]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #48]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #32]             // 16-byte Folded Reload
	add	sp, sp, #128                    // =128
	hint	#29
	ret
.LBB114_12:
	bl	__stack_chk_fail
.Lfunc_end114:
	.size	ftl_scan_all_data, .Lfunc_end114-ftl_scan_all_data
                                        // -- End function
	.globl	update_multiplier_value         // -- Begin function update_multiplier_value
	.p2align	2
	.type	update_multiplier_value,@function
update_multiplier_value:                // @update_multiplier_value
// %bb.0:
	hint	#25
	adrp	x8, c_ftl_nand_planes_num
	ldrh	w9, [x8, :lo12:c_ftl_nand_planes_num]
	mov	w8, wzr
                                        // kill: def $w0 killed $w0 def $x0
	cbz	x9, .LBB115_6
// %bb.1:
	adrp	x10, c_ftl_nand_planes_per_die
	adrp	x11, c_ftl_nand_blks_per_die
	adrp	x12, c_ftl_nand_page_pre_blk
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_per_die]
	ldrh	w11, [x11, :lo12:c_ftl_nand_blks_per_die]
	ldrh	w12, [x12, :lo12:c_ftl_nand_page_pre_blk]
	adrp	x13, p_plane_order_table
	adrp	x15, gBbtInfo
	add	x13, x13, :lo12:p_plane_order_table
	mul	w14, w10, w0
	add	x15, x15, :lo12:gBbtInfo
.LBB115_2:                              // =>This Inner Loop Header: Depth=1
	ldrb	w16, [x13], #1
	and	w17, w10, #0xffff
	and	w18, w16, #0xffff
	udiv	w17, w18, w17
	msub	w16, w17, w10, w16
	madd	w17, w11, w17, w14
	add	w16, w17, w16
	and	w18, w11, #0xffff
	and	w17, w16, #0xffff
	udiv	w17, w17, w18
	add	x18, x15, w17, uxtw #3
	ldr	x18, [x18, #32]
	msub	w16, w17, w11, w16
	lsr	w17, w16, #3
	and	x17, x17, #0x1ffc
	ldr	w17, [x18, x17]
	lsr	w16, w17, w16
	tst	w16, #0x1
	csel	w16, w12, wzr, eq
	subs	x9, x9, #1                      // =1
	add	w8, w16, w8
	b.ne	.LBB115_2
// %bb.3:
	tst	w8, #0xffff
	b.eq	.LBB115_5
// %bb.4:
	and	w8, w8, #0xffff
	mov	w9, #32768
	udiv	w8, w9, w8
	b	.LBB115_6
.LBB115_5:
	mov	w8, wzr
.LBB115_6:
	adrp	x9, p_data_block_list_table
	ldr	x9, [x9, :lo12:p_data_block_list_table]
	and	x10, x0, #0xffff
	mov	w11, #6
	mov	w0, wzr
	madd	x9, x10, x11, x9
	strh	w8, [x9, #4]
	hint	#29
	ret
.Lfunc_end115:
	.size	update_multiplier_value, .Lfunc_end115-update_multiplier_value
                                        // -- End function
	.globl	GetSwlReplaceBlock              // -- Begin function GetSwlReplaceBlock
	.p2align	2
	.type	GetSwlReplaceBlock,@function
GetSwlReplaceBlock:                     // @GetSwlReplaceBlock
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	adrp	x11, g_totle_avg_erase_count
	adrp	x9, g_min_erase_count
	ldr	w2, [x11, :lo12:g_totle_avg_erase_count]
	ldr	w10, [x9, :lo12:g_min_erase_count]
	adrp	x8, g_max_erase_count
	str	x19, [sp, #16]                  // 8-byte Folded Spill
	mov	x29, sp
	cmp	w2, w10
	b.hs	.LBB116_5
// %bb.1:
	adrp	x12, c_ftl_nand_data_blks_per_plane
	ldrh	w13, [x12, :lo12:c_ftl_nand_data_blks_per_plane]
	adrp	x12, g_totle_mlc_erase_count
	mov	w14, wzr
	str	wzr, [x12, :lo12:g_totle_mlc_erase_count]
	cbz	w13, .LBB116_4
// %bb.2:
	adrp	x15, p_erase_count_table
	ldr	x15, [x15, :lo12:p_erase_count_table]
	cmp	w13, #1                         // =1
	csinc	w16, w13, wzr, hi
.LBB116_3:                              // =>This Inner Loop Header: Depth=1
	ldrh	w17, [x15], #2
	subs	x16, x16, #1                    // =1
	add	w14, w14, w17
	str	w14, [x12, :lo12:g_totle_mlc_erase_count]
	b.ne	.LBB116_3
.LBB116_4:
	adrp	x15, g_totle_slc_erase_count
	ldr	w15, [x15, :lo12:g_totle_slc_erase_count]
	adrp	x16, c_mlc_erase_count_value
	ldrh	w16, [x16, :lo12:c_mlc_erase_count_value]
	udiv	w2, w14, w13
	str	w2, [x11, :lo12:g_totle_avg_erase_count]
	sub	w11, w14, w15
	udiv	w11, w11, w16
	str	w11, [x12, :lo12:g_totle_mlc_erase_count]
	b	.LBB116_10
.LBB116_5:
	ldr	w13, [x8, :lo12:g_max_erase_count]
	cmp	w2, w13
	b.ls	.LBB116_10
// %bb.6:
	adrp	x12, c_ftl_nand_data_blks_per_plane
	ldrh	w14, [x12, :lo12:c_ftl_nand_data_blks_per_plane]
	add	w13, w13, #1                    // =1
	str	w13, [x8, :lo12:g_max_erase_count]
	cbz	w14, .LBB116_10
// %bb.7:
	mov	x10, xzr
	adrp	x13, p_erase_count_table
.LBB116_8:                              // =>This Inner Loop Header: Depth=1
	ldr	x14, [x13, :lo12:p_erase_count_table]
	lsl	x15, x10, #1
	add	x10, x10, #1                    // =1
	ldrh	w16, [x14, x15]
	add	w16, w16, #1                    // =1
	strh	w16, [x14, x15]
	ldrh	w14, [x12, :lo12:c_ftl_nand_data_blks_per_plane]
	cmp	x10, x14
	b.lo	.LBB116_8
// %bb.9:
	ldr	w10, [x9, :lo12:g_min_erase_count]
	ldr	w2, [x11, :lo12:g_totle_avg_erase_count]
.LBB116_10:
	add	w11, w10, #256                  // =256
	cmp	w11, w2
	b.ls	.LBB116_13
// %bb.11:
	ldr	w11, [x8, :lo12:g_max_erase_count]
	add	w12, w10, #768                  // =768
	cmp	w12, w11
	b.ls	.LBB116_13
.LBB116_12:
	mov	w0, #65535
	b	.LBB116_43
.LBB116_13:
	adrp	x11, p_free_data_block_list_head
	ldr	x11, [x11, :lo12:p_free_data_block_list_head]
	cbz	x11, .LBB116_18
// %bb.14:
	adrp	x12, g_num_free_superblocks
	ldrh	w13, [x12, :lo12:g_num_free_superblocks]
	adrp	x12, p_data_block_list_table
	ldr	x12, [x12, :lo12:p_data_block_list_table]
	lsl	w15, w13, #3
	add	w14, w13, w13, lsl #1
	sub	w13, w15, w13
	lsr	w14, w14, #2
	lsr	w13, w13, #3
	cmp	w13, w14
	sub	x16, x11, x12
	csel	w13, w13, w14, lo
	mov	w14, #43691
	lsr	x15, x16, #1
	movk	w14, #43690, lsl #16
	mul	w16, w15, w14
	cbz	w13, .LBB116_19
// %bb.15:
	mov	w14, wzr
	mov	w15, #6
	mov	w17, #65535
.LBB116_16:                             // =>This Inner Loop Header: Depth=1
	and	x18, x16, #0xffff
	mul	x18, x18, x15
	ldrh	w18, [x12, x18]
	mov	w0, w16
	cmp	w18, w17
	b.eq	.LBB116_20
// %bb.17:                              //   in Loop: Header=BB116_16 Depth=1
	add	w14, w14, #1                    // =1
	and	w16, w14, #0xffff
	cmp	w16, w13, uxth
	mov	w16, w18
	mov	w0, w18
	b.lo	.LBB116_16
	b	.LBB116_20
.LBB116_18:
	mov	w6, wzr
	b	.LBB116_21
.LBB116_19:
	mov	w0, w16
.LBB116_20:
	adrp	x12, p_erase_count_table
	ldr	x12, [x12, :lo12:p_erase_count_table]
	and	x13, x0, #0xffff
	ldrh	w6, [x12, x13, lsl #1]
.LBB116_21:
	add	w12, w10, #64                   // =64
	cmp	w12, w6
	mov	w0, #65535
	b.hi	.LBB116_43
// %bb.22:
	adrp	x12, p_data_block_list_head
	ldr	x5, [x12, :lo12:p_data_block_list_head]
	cbz	x5, .LBB116_43
// %bb.23:
	ldrh	w15, [x5]
	mov	w0, #65535
	cmp	w15, w0
	b.eq	.LBB116_43
// %bb.24:
	adrp	x12, c_ftl_nand_data_blks_per_plane
	adrp	x13, p_data_block_list_table
	adrp	x14, p_erase_count_table
	ldrh	w17, [x12, :lo12:c_ftl_nand_data_blks_per_plane]
	ldr	x18, [x13, :lo12:p_data_block_list_table]
	ldr	x1, [x14, :lo12:p_erase_count_table]
	mov	x4, #-6148914691236517206
	mov	w16, wzr
	mov	w3, #6
	movk	x4, #43691
	mov	w12, #65535
	mov	w19, #65535
	b	.LBB116_26
.LBB116_25:                             //   in Loop: Header=BB116_26 Depth=1
	and	x15, x15, #0xffff
	madd	x5, x15, x3, x18
	ldrh	w15, [x5]
	cmp	w15, w0
	b.eq	.LBB116_31
.LBB116_26:                             // =>This Inner Loop Header: Depth=1
	add	w16, w16, #1                    // =1
	cmp	w17, w16, uxth
	b.lo	.LBB116_12
// %bb.27:                              //   in Loop: Header=BB116_26 Depth=1
	ldrh	w7, [x5, #4]
	cbz	w7, .LBB116_25
// %bb.28:                              //   in Loop: Header=BB116_26 Depth=1
	sub	x5, x5, x18
	asr	x5, x5, #1
	mul	x5, x5, x4
	and	x7, x5, #0xffff
	ldrh	w7, [x1, x7, lsl #1]
	cmp	w10, w7
	b.hs	.LBB116_30
// %bb.29:                              //   in Loop: Header=BB116_26 Depth=1
	cmp	w7, w12, uxth
	csel	w19, w5, w19, lo
	csel	w12, w7, w12, lo
	b	.LBB116_25
.LBB116_30:
	mov	w19, w5
.LBB116_31:
	and	w1, w19, #0xffff
	mov	w0, #65535
	cmp	w1, w0
	b.eq	.LBB116_43
// %bb.32:
	ldr	x14, [x14, :lo12:p_erase_count_table]
	and	x15, x19, #0xffff
	ldrh	w16, [x14, x15, lsl #1]
	cmp	w10, w16
	b.hs	.LBB116_37
// %bb.33:
	cbz	x11, .LBB116_35
// %bb.34:
	ldr	w13, [x13, :lo12:p_data_block_list_table]
	sub	w11, w11, w13
	mov	w13, #43691
	lsr	w11, w11, #1
	movk	w13, #43690, lsl #16
	mul	w11, w11, w13
	and	x11, x11, #0xffff
	ldrh	w11, [x14, x11, lsl #1]
.LBB116_35:
	cmp	w10, w11
	b.hs	.LBB116_37
// %bb.36:
	and	w10, w12, #0xffff
	str	w10, [x9, :lo12:g_min_erase_count]
.LBB116_37:
	cmp	w2, w16
	mov	w0, #65535
	b.ls	.LBB116_43
// %bb.38:
	add	w9, w16, #128                   // =128
	cmp	w9, w6
	b.hs	.LBB116_43
// %bb.39:
	add	w9, w16, #256                   // =256
	cmp	w9, w2
	b.hs	.LBB116_41
// %bb.40:
	ldr	w3, [x8, :lo12:g_max_erase_count]
	b	.LBB116_42
.LBB116_41:
	ldr	w3, [x8, :lo12:g_max_erase_count]
	add	w8, w16, #768                   // =768
	cmp	w8, w3
	b.hs	.LBB116_12
.LBB116_42:
	adrp	x8, p_valid_page_count_table
	ldr	x8, [x8, :lo12:p_valid_page_count_table]
	lsl	x9, x15, #1
	ldrh	w5, [x14, x9]
	adrp	x0, .L.str.109
	ldrh	w4, [x8, x9]
	add	x0, x0, :lo12:.L.str.109
	bl	sftl_printk
	adrp	x8, g_in_swl_replace
	mov	w9, #1
	str	w9, [x8, :lo12:g_in_swl_replace]
	mov	w0, w19
.LBB116_43:
	ldr	x19, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end116:
	.size	GetSwlReplaceBlock, .Lfunc_end116-GetSwlReplaceBlock
                                        // -- End function
	.globl	free_data_superblock            // -- Begin function free_data_superblock
	.p2align	2
	.type	free_data_superblock,@function
free_data_superblock:                   // @free_data_superblock
// %bb.0:
	hint	#34
                                        // kill: def $w0 killed $w0 def $x0
	mvn	w8, w0
	tst	w8, #0xffff
	b.eq	.LBB117_2
// %bb.1:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	adrp	x8, p_valid_page_count_table
	ldr	x8, [x8, :lo12:p_valid_page_count_table]
	and	x9, x0, #0xffff
	mov	x29, sp
                                        // kill: def $w0 killed $w0 killed $x0
	strh	wzr, [x8, x9, lsl #1]
	bl	INSERT_FREE_LIST
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
.LBB117_2:
	mov	w0, wzr
	ret
.Lfunc_end117:
	.size	free_data_superblock, .Lfunc_end117-free_data_superblock
                                        // -- End function
	.globl	allocate_data_superblock        // -- Begin function allocate_data_superblock
	.p2align	2
	.type	allocate_data_superblock,@function
allocate_data_superblock:               // @allocate_data_superblock
// %bb.0:
	hint	#25
	sub	sp, sp, #112                    // =112
	stp	x26, x25, [sp, #48]             // 16-byte Folded Spill
	adrp	x26, .L.str.78
	stp	x28, x27, [sp, #32]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #64]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #80]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #96]             // 16-byte Folded Spill
	mov	x19, x0
	add	x24, x0, #16                    // =16
	adrp	x23, g_num_free_superblocks
	adrp	x28, g_num_data_superblocks
	adrp	x21, c_ftl_nand_data_blks_per_plane
	add	x26, x26, :lo12:.L.str.78
	adrp	x27, p_valid_page_count_table
	mov	w22, #65535
	adrp	x20, req_erase
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	add	x29, sp, #16                    // =16
.LBB118_1:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB118_16 Depth 2
                                        //     Child Loop BB118_35 Depth 2
                                        //     Child Loop BB118_39 Depth 2
                                        //     Child Loop BB118_44 Depth 2
	ldrh	w8, [x23, :lo12:g_num_free_superblocks]
	ldrh	w9, [x28, :lo12:g_num_data_superblocks]
	ldrh	w10, [x21, :lo12:c_ftl_nand_data_blks_per_plane]
	add	w8, w9, w8
	cmp	w8, w10
	b.ls	.LBB118_3
// %bb.2:                               //   in Loop: Header=BB118_1 Depth=1
	adrp	x1, .L__func__.allocate_data_superblock
	mov	w2, #2667
	mov	x0, x26
	add	x1, x1, :lo12:.L__func__.allocate_data_superblock
	bl	sftl_printk
.LBB118_3:                              //   in Loop: Header=BB118_1 Depth=1
	adrp	x8, g_gc_temp_superblock
	add	x8, x8, :lo12:g_gc_temp_superblock
	cmp	x19, x8
	b.eq	.LBB118_5
.LBB118_4:                              //   in Loop: Header=BB118_1 Depth=1
	mov	w1, wzr
	b	.LBB118_6
.LBB118_5:                              //   in Loop: Header=BB118_1 Depth=1
	adrp	x9, g_in_swl_replace
	ldrh	w8, [x23, :lo12:g_num_free_superblocks]
	ldr	w9, [x9, :lo12:g_in_swl_replace]
	mul	w9, w9, w8
	lsr	w9, w9, #2
	add	w1, w9, w8, lsr #1
	mvn	w8, w1
	tst	w8, #0xffff
	b.eq	.LBB118_4
.LBB118_6:                              //   in Loop: Header=BB118_1 Depth=1
	adrp	x0, p_free_data_block_list_head
	add	x0, x0, :lo12:p_free_data_block_list_head
	bl	List_pop_index_node
	ldrh	w8, [x23, :lo12:g_num_free_superblocks]
	mov	w25, w0
	cbnz	w8, .LBB118_8
// %bb.7:                               //   in Loop: Header=BB118_1 Depth=1
	adrp	x1, .L__func__.allocate_data_superblock
	mov	w2, #2676
	mov	x0, x26
	add	x1, x1, :lo12:.L__func__.allocate_data_superblock
	bl	sftl_printk
	ldrh	w8, [x23, :lo12:g_num_free_superblocks]
.LBB118_8:                              //   in Loop: Header=BB118_1 Depth=1
	ldrh	w9, [x21, :lo12:c_ftl_nand_data_blks_per_plane]
	sub	w10, w8, #1                     // =1
	and	w8, w25, #0xffff
	strh	w10, [x23, :lo12:g_num_free_superblocks]
	cmp	w8, w9
	b.hs	.LBB118_1
// %bb.9:                               //   in Loop: Header=BB118_1 Depth=1
	ldr	x9, [x27, :lo12:p_valid_page_count_table]
	ldrh	w9, [x9, w8, uxtw #1]
	mov	w8, w8
	cbnz	w9, .LBB118_1
// %bb.10:                              //   in Loop: Header=BB118_1 Depth=1
	mov	x0, x19
	str	x8, [sp, #8]                    // 8-byte Folded Spill
	strh	w25, [x19]
	bl	make_superblock
	ldrb	w8, [x19, #7]
	cbz	w8, .LBB118_25
// %bb.11:                              //   in Loop: Header=BB118_1 Depth=1
	ldrh	w8, [x23, :lo12:g_num_free_superblocks]
	ldrh	w9, [x28, :lo12:g_num_data_superblocks]
	ldrh	w10, [x21, :lo12:c_ftl_nand_data_blks_per_plane]
	add	w8, w9, w8
	cmp	w8, w10
	b.ls	.LBB118_13
// %bb.12:                              //   in Loop: Header=BB118_1 Depth=1
	adrp	x1, .L__func__.allocate_data_superblock
	mov	w2, #2693
	mov	x0, x26
	add	x1, x1, :lo12:.L__func__.allocate_data_superblock
	bl	sftl_printk
.LBB118_13:                             //   in Loop: Header=BB118_1 Depth=1
	adrp	x13, c_ftl_nand_planes_num
	ldrh	w9, [x13, :lo12:c_ftl_nand_planes_num]
	cbz	w9, .LBB118_20
// %bb.14:                              //   in Loop: Header=BB118_1 Depth=1
	mov	x8, xzr
	mov	x10, xzr
	mov	w21, wzr
	b	.LBB118_16
.LBB118_15:                             //   in Loop: Header=BB118_16 Depth=2
	add	x10, x10, #1                    // =1
	cmp	x10, w9, uxth
	add	x8, x8, #32                     // =32
	b.hs	.LBB118_18
.LBB118_16:                             //   Parent Loop BB118_1 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldr	x11, [x20, :lo12:req_erase]
	add	x11, x11, x8
	str	xzr, [x11, #8]
	ldr	x11, [x20, :lo12:req_erase]
	add	x11, x11, x8
	str	xzr, [x11, #16]
	ldrh	w11, [x24, x10, lsl #1]
	cmp	w11, w22
	b.eq	.LBB118_15
// %bb.17:                              //   in Loop: Header=BB118_16 Depth=2
	ldr	x9, [x20, :lo12:req_erase]
	and	x12, x21, #0xffff
	lsl	w11, w11, #10
	add	w21, w21, #1                    // =1
	add	x9, x9, x12, lsl #5
	str	w11, [x9, #4]
	ldrh	w9, [x13, :lo12:c_ftl_nand_planes_num]
	b	.LBB118_15
.LBB118_18:                             //   in Loop: Header=BB118_1 Depth=1
	ands	w2, w21, #0xffff
	b.eq	.LBB118_20
// %bb.19:                              //   in Loop: Header=BB118_1 Depth=1
	mov	w26, wzr
	b	.LBB118_21
.LBB118_20:                             //   in Loop: Header=BB118_1 Depth=1
	adrp	x1, .L__func__.allocate_data_superblock
	mov	w2, #2704
	mov	x0, x26
	add	x1, x1, :lo12:.L__func__.allocate_data_superblock
	bl	sftl_printk
	mov	w2, wzr
	mov	w21, wzr
	mov	w26, #1
.LBB118_21:                             //   in Loop: Header=BB118_1 Depth=1
	adrp	x8, g_gc_superblock
	ldrh	w8, [x8, :lo12:g_gc_superblock]
	ldr	x11, [sp, #8]                   // 8-byte Folded Reload
	cmp	w8, w25, uxth
	b.ne	.LBB118_23
// %bb.22:                              //   in Loop: Header=BB118_1 Depth=1
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.allocate_data_superblock
	mov	x23, x28
	mov	w28, w26
	mov	x26, x27
	mov	w27, w2
	mov	w2, #2706
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.allocate_data_superblock
	bl	sftl_printk
	ldr	x11, [sp, #8]                   // 8-byte Folded Reload
	mov	w2, w27
	mov	x27, x26
	mov	w26, w28
	mov	x28, x23
	adrp	x23, g_num_free_superblocks
.LBB118_23:                             //   in Loop: Header=BB118_1 Depth=1
	adrp	x8, p_erase_count_table
	ldr	x8, [x8, :lo12:p_erase_count_table]
	ldrb	w10, [x19, #8]
	ldrh	w9, [x8, x11, lsl #1]
	cbz	w10, .LBB118_29
// %bb.24:                              //   in Loop: Header=BB118_1 Depth=1
	adrp	x10, g_totle_slc_erase_count
	add	w9, w9, #1                      // =1
	add	x10, x10, :lo12:g_totle_slc_erase_count
	b	.LBB118_30
.LBB118_25:                             //   in Loop: Header=BB118_1 Depth=1
	ldr	x8, [x27, :lo12:p_valid_page_count_table]
	ldr	x9, [sp, #8]                    // 8-byte Folded Reload
	mov	w0, w25
	strh	w22, [x8, x9, lsl #1]
	bl	insert_data_list
	ldrh	w8, [x28, :lo12:g_num_data_superblocks]
	ldrh	w9, [x21, :lo12:c_ftl_nand_data_blks_per_plane]
	add	w8, w8, #1                      // =1
	cmp	w9, w8, uxth
	strh	w8, [x28, :lo12:g_num_data_superblocks]
	b.hs	.LBB118_27
// %bb.26:                              //   in Loop: Header=BB118_1 Depth=1
	adrp	x1, .L__func__.INSERT_DATA_LIST
	mov	w2, #214
	mov	x0, x26
	add	x1, x1, :lo12:.L__func__.INSERT_DATA_LIST
	bl	sftl_printk
	ldrh	w8, [x28, :lo12:g_num_data_superblocks]
	ldrh	w9, [x21, :lo12:c_ftl_nand_data_blks_per_plane]
.LBB118_27:                             //   in Loop: Header=BB118_1 Depth=1
	ldrh	w10, [x23, :lo12:g_num_free_superblocks]
	add	w8, w10, w8, uxth
	cmp	w8, w9
	b.ls	.LBB118_1
// %bb.28:                              //   in Loop: Header=BB118_1 Depth=1
	adrp	x1, .L__func__.allocate_data_superblock
	mov	w2, #2690
	mov	x0, x26
	add	x1, x1, :lo12:.L__func__.allocate_data_superblock
	bl	sftl_printk
	b	.LBB118_1
.LBB118_29:                             //   in Loop: Header=BB118_1 Depth=1
	adrp	x10, c_mlc_erase_count_value
	ldrh	w10, [x10, :lo12:c_mlc_erase_count_value]
	cmp	w9, #0                          // =0
	add	w10, w10, w9
	mov	w9, #2
	csel	w9, w9, w10, eq
	adrp	x10, g_totle_mlc_erase_count
	add	x10, x10, :lo12:g_totle_mlc_erase_count
.LBB118_30:                             //   in Loop: Header=BB118_1 Depth=1
	lsl	x11, x11, #1
	strh	w9, [x8, x11]
	ldr	w8, [x10]
	adrp	x9, p_erase_count_table
	ldr	x9, [x9, :lo12:p_erase_count_table]
	add	w8, w8, #1                      // =1
	str	w8, [x10]
	ldrh	w8, [x9, x11]
	adrp	x9, g_max_erase_count
	ldr	w9, [x9, :lo12:g_max_erase_count]
	cmp	w9, w8
	b.hs	.LBB118_32
// %bb.31:                              //   in Loop: Header=BB118_1 Depth=1
	adrp	x9, g_max_erase_count
	str	w8, [x9, :lo12:g_max_erase_count]
.LBB118_32:                             //   in Loop: Header=BB118_1 Depth=1
	adrp	x8, g_totle_mlc_erase_count
	adrp	x9, c_mlc_erase_count_value
	adrp	x10, g_totle_slc_erase_count
	ldr	w8, [x8, :lo12:g_totle_mlc_erase_count]
	ldrh	w9, [x9, :lo12:c_mlc_erase_count_value]
	ldr	w10, [x10, :lo12:g_totle_slc_erase_count]
	adrp	x11, c_ftl_nand_data_blks_per_plane
	ldrh	w11, [x11, :lo12:c_ftl_nand_data_blks_per_plane]
	adrp	x12, gp_ect_tbl_info
	ldr	x12, [x12, :lo12:gp_ect_tbl_info]
	madd	w8, w8, w9, w10
	udiv	w8, w8, w11
	adrp	x9, g_totle_avg_erase_count
	str	w8, [x9, :lo12:g_totle_avg_erase_count]
	ldr	w8, [x12, #16]
	add	w8, w8, #1                      // =1
	str	w8, [x12, #16]
	cbz	w26, .LBB118_34
// %bb.33:                              //   in Loop: Header=BB118_1 Depth=1
	ldr	x0, [x20, :lo12:req_erase]
	bl	FlashEraseBlocks
	b	.LBB118_50
.LBB118_34:                             //   in Loop: Header=BB118_1 Depth=1
                                        // kill: def $w21 killed $w21 killed $x21 def $x21
	mov	w9, #4
	and	x21, x21, #0xffff
	mov	x8, x21
.LBB118_35:                             //   Parent Loop BB118_1 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldr	x10, [x20, :lo12:req_erase]
	subs	x8, x8, #1                      // =1
	ldr	w11, [x10, x9]
	and	w11, w11, #0xfffffc00
	str	w11, [x10, x9]
	add	x9, x9, #32                     // =32
	b.ne	.LBB118_35
// %bb.36:                              //   in Loop: Header=BB118_1 Depth=1
	ldr	x0, [x20, :lo12:req_erase]
	bl	FlashEraseBlocks
	adrp	x10, c_ftl_nand_planes_num
	tbnz	w26, #0, .LBB118_50
// %bb.37:                              //   in Loop: Header=BB118_1 Depth=1
	mov	x27, xzr
	mov	w26, wzr
	mov	x28, x24
	b	.LBB118_39
.LBB118_38:                             //   in Loop: Header=BB118_39 Depth=2
	add	x27, x27, #32                   // =32
	subs	x21, x21, #1                    // =1
	add	x28, x28, #2                    // =2
	b.eq	.LBB118_41
.LBB118_39:                             //   Parent Loop BB118_1 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldr	x8, [x20, :lo12:req_erase]
	ldr	w9, [x8, x27]
	cmn	w9, #1                          // =1
	b.ne	.LBB118_38
// %bb.40:                              //   in Loop: Header=BB118_39 Depth=2
	add	x8, x8, x27
	ldr	w8, [x8, #4]
	adrp	x9, c_ftl_nand_blks_per_die
	ldrh	w9, [x9, :lo12:c_ftl_nand_blks_per_die]
	adrp	x22, gBbtInfo
	ubfx	w1, w8, #10, #16
	add	x22, x22, :lo12:gBbtInfo
	udiv	w2, w1, w9
	lsr	w10, w8, #10
	add	x8, x22, w2, uxtw #3
	ldr	x8, [x8, #32]
	msub	w9, w2, w9, w10
	ubfx	w10, w9, #3, #13
	and	x10, x10, #0x1ffc
	ldr	w11, [x8, x10]
	mov	w12, #1
	and	w3, w9, #0xffff
	lsl	w9, w12, w9
	adrp	x0, .L.str.82
	orr	w4, w9, w11
	add	x0, x0, :lo12:.L.str.82
	add	w26, w26, #1                    // =1
	str	w4, [x8, x10]
	bl	sftl_printk
	ldrh	w8, [x22, #6]
	adrp	x10, c_ftl_nand_planes_num
	add	w8, w8, #1                      // =1
	strh	w8, [x22, #6]
	mov	w22, #65535
	strh	w22, [x28]
	ldrb	w8, [x19, #7]
	sub	w8, w8, #1                      // =1
	strb	w8, [x19, #7]
	b	.LBB118_38
.LBB118_41:                             //   in Loop: Header=BB118_1 Depth=1
	cmp	w26, #1                         // =1
	adrp	x27, p_valid_page_count_table
	adrp	x28, g_num_data_superblocks
	b.lt	.LBB118_50
// %bb.42:                              //   in Loop: Header=BB118_1 Depth=1
	ldrh	w9, [x10, :lo12:c_ftl_nand_planes_num]
	cbz	x9, .LBB118_47
// %bb.43:                              //   in Loop: Header=BB118_1 Depth=1
	adrp	x10, c_ftl_nand_planes_per_die
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_per_die]
	adrp	x11, c_ftl_nand_blks_per_die
	adrp	x12, c_ftl_nand_page_pre_blk
	ldrh	w11, [x11, :lo12:c_ftl_nand_blks_per_die]
	ldrh	w12, [x12, :lo12:c_ftl_nand_page_pre_blk]
	adrp	x14, p_plane_order_table
	adrp	x18, gBbtInfo
	mov	w8, wzr
	mul	w13, w10, w25
	add	x14, x14, :lo12:p_plane_order_table
	add	x18, x18, :lo12:gBbtInfo
.LBB118_44:                             //   Parent Loop BB118_1 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldrb	w15, [x14], #1
	and	w16, w10, #0xffff
	and	w17, w15, #0xffff
	udiv	w16, w17, w16
	msub	w15, w16, w10, w15
	madd	w16, w16, w11, w13
	add	w15, w16, w15
	and	w17, w11, #0xffff
	and	w16, w15, #0xffff
	udiv	w16, w16, w17
	add	x17, x18, w16, uxtw #3
	ldr	x17, [x17, #32]
	msub	w15, w16, w11, w15
	lsr	w16, w15, #3
	and	x16, x16, #0x1ffc
	ldr	w16, [x17, x16]
	lsr	w15, w16, w15
	tst	w15, #0x1
	csel	w15, w12, wzr, eq
	subs	x9, x9, #1                      // =1
	add	w8, w15, w8
	b.ne	.LBB118_44
// %bb.45:                              //   in Loop: Header=BB118_1 Depth=1
	ldr	x11, [sp, #8]                   // 8-byte Folded Reload
	tst	w8, #0xffff
	b.eq	.LBB118_48
// %bb.46:                              //   in Loop: Header=BB118_1 Depth=1
	and	w8, w8, #0xffff
	mov	w9, #32768
	udiv	w8, w9, w8
	b	.LBB118_49
.LBB118_47:                             //   in Loop: Header=BB118_1 Depth=1
	ldr	x11, [sp, #8]                   // 8-byte Folded Reload
.LBB118_48:                             //   in Loop: Header=BB118_1 Depth=1
	mov	w8, wzr
.LBB118_49:                             //   in Loop: Header=BB118_1 Depth=1
	adrp	x9, p_data_block_list_table
	ldr	x9, [x9, :lo12:p_data_block_list_table]
	mov	w10, #6
	madd	x9, x11, x10, x9
	strh	w8, [x9, #4]
	bl	FtlBbmTblFlush
.LBB118_50:                             //   in Loop: Header=BB118_1 Depth=1
	ldrb	w8, [x19, #7]
	cbnz	w8, .LBB118_52
// %bb.51:                              //   in Loop: Header=BB118_1 Depth=1
	ldr	x8, [x27, :lo12:p_valid_page_count_table]
	ldr	x9, [sp, #8]                    // 8-byte Folded Reload
	adrp	x26, .L.str.78
	adrp	x21, c_ftl_nand_data_blks_per_plane
	add	x26, x26, :lo12:.L.str.78
	strh	w22, [x8, x9, lsl #1]
	b	.LBB118_1
.LBB118_52:
	adrp	x9, c_ftl_nand_page_pre_blk
	ldrh	w9, [x9, :lo12:c_ftl_nand_page_pre_blk]
	strh	w25, [x19]
	strh	wzr, [x19, #2]
	strb	wzr, [x19, #6]
	mul	w8, w9, w8
	adrp	x10, g_GlobalSysVersion
	strh	w8, [x19, #4]
	ldr	w9, [x10, :lo12:g_GlobalSysVersion]
	str	w9, [x19, #12]
	add	w9, w9, #1                      // =1
	str	w9, [x10, :lo12:g_GlobalSysVersion]
	ldr	x9, [x27, :lo12:p_valid_page_count_table]
	ldrh	w10, [x19]
	strh	w8, [x9, x10, lsl #1]
	ldrh	w8, [x19, #4]
	cbz	w8, .LBB118_54
// %bb.53:
	ldrb	w8, [x19, #7]
	cbnz	w8, .LBB118_55
.LBB118_54:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.allocate_data_superblock
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.allocate_data_superblock
	mov	w2, #2759
	bl	sftl_printk
.LBB118_55:
	ldp	x20, x19, [sp, #96]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #80]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #64]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #48]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	add	sp, sp, #112                    // =112
	hint	#29
	ret
.Lfunc_end118:
	.size	allocate_data_superblock, .Lfunc_end118-allocate_data_superblock
                                        // -- End function
	.globl	FtlGcFreeBadSuperBlk            // -- Begin function FtlGcFreeBadSuperBlk
	.p2align	2
	.type	FtlGcFreeBadSuperBlk,@function
FtlGcFreeBadSuperBlk:                   // @FtlGcFreeBadSuperBlk
// %bb.0:
	hint	#25
	sub	sp, sp, #112                    // =112
	stp	x26, x25, [sp, #48]             // 16-byte Folded Spill
	adrp	x26, g_gc_bad_block_temp_num
	ldrh	w8, [x26, :lo12:g_gc_bad_block_temp_num]
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	add	x29, sp, #16                    // =16
	stp	x28, x27, [sp, #32]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #64]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #80]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #96]             // 16-byte Folded Spill
	stur	w0, [x29, #-4]                  // 4-byte Folded Spill
	cbz	w8, .LBB119_28
// %bb.1:
	adrp	x9, c_ftl_nand_planes_num
	ldrh	w9, [x9, :lo12:c_ftl_nand_planes_num]
	adrp	x24, g_gc_bad_block_temp_tbl
	add	x24, x24, :lo12:g_gc_bad_block_temp_tbl
	cbz	w9, .LBB119_17
// %bb.2:
	adrp	x27, gBbtInfo
	adrp	x21, .L.str.82
	mov	x28, xzr
	add	x27, x27, :lo12:gBbtInfo
	add	x21, x21, :lo12:.L.str.82
	mov	w10, #1
	b	.LBB119_5
.LBB119_3:                              //   in Loop: Header=BB119_5 Depth=1
	adrp	x9, c_ftl_nand_planes_num
	ldrh	w9, [x9, :lo12:c_ftl_nand_planes_num]
	mov	w10, w8
.LBB119_4:                              //   in Loop: Header=BB119_5 Depth=1
	add	x28, x28, #1                    // =1
	cmp	x28, w9, uxth
	b.hs	.LBB119_17
.LBB119_5:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB119_9 Depth 2
                                        //       Child Loop BB119_13 Depth 3
                                        //       Child Loop BB119_15 Depth 3
	tst	w10, #0xffff
	b.eq	.LBB119_16
// %bb.6:                               //   in Loop: Header=BB119_5 Depth=1
	adrp	x9, p_plane_order_table
	add	x9, x9, :lo12:p_plane_order_table
	adrp	x10, c_ftl_nand_planes_per_die
	ldrb	w9, [x9, x28]
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_per_die]
	adrp	x11, c_ftl_nand_blks_per_die
	ldur	w14, [x29, #-4]                 // 4-byte Folded Reload
	ldrh	w11, [x11, :lo12:c_ftl_nand_blks_per_die]
	and	w12, w9, #0xffff
	and	w13, w10, #0xffff
	mul	w14, w10, w14
	udiv	w12, w12, w13
	madd	w11, w11, w12, w14
	msub	w9, w12, w10, w9
	add	w23, w11, w9
	adrp	x20, g_gc_bad_block_temp_tbl+2
	mov	x19, xzr
	mov	x25, xzr
	and	w22, w23, #0xffff
	add	x20, x20, :lo12:g_gc_bad_block_temp_tbl+2
	b	.LBB119_9
.LBB119_7:                              //   in Loop: Header=BB119_9 Depth=2
	sub	w8, w8, #1                      // =1
	strh	w8, [x26, :lo12:g_gc_bad_block_temp_num]
.LBB119_8:                              //   in Loop: Header=BB119_9 Depth=2
	add	x25, x25, #1                    // =1
	add	x20, x20, #2                    // =2
	cmp	x25, w8, uxth
	sub	x19, x19, #1                    // =1
	b.hs	.LBB119_3
.LBB119_9:                              //   Parent Loop BB119_5 Depth=1
                                        // =>  This Loop Header: Depth=2
                                        //       Child Loop BB119_13 Depth 3
                                        //       Child Loop BB119_15 Depth 3
	ldrh	w9, [x24, x25, lsl #1]
	cmp	w9, w23, uxth
	b.ne	.LBB119_8
// %bb.10:                              //   in Loop: Header=BB119_9 Depth=2
	adrp	x0, .L.str.114
	add	x0, x0, :lo12:.L.str.114
	mov	w1, w22
	bl	sftl_printk
	adrp	x8, c_ftl_nand_blks_per_die
	ldrh	w8, [x8, :lo12:c_ftl_nand_blks_per_die]
	and	w9, w23, #0xffff
	mov	w12, #1
	mov	x0, x21
	udiv	w2, w9, w8
	add	x9, x27, w2, uxtw #3
	ldr	x9, [x9, #32]
	msub	w8, w2, w8, w23
	ubfx	w10, w8, #3, #13
	and	x10, x10, #0x1ffc
	ldr	w11, [x9, x10]
	and	w3, w8, #0xffff
	lsl	w8, w12, w8
	mov	w1, w22
	orr	w4, w8, w11
	str	w4, [x9, x10]
	bl	sftl_printk
	ldrh	w8, [x27, #6]
	add	w8, w8, #1                      // =1
	strh	w8, [x27, #6]
	bl	FtlBbmTblFlush
	ldrh	w8, [x26, :lo12:g_gc_bad_block_temp_num]
	cmp	x25, x8
	b.hs	.LBB119_7
// %bb.11:                              //   in Loop: Header=BB119_9 Depth=2
	sub	x10, x8, x25
	cmp	x10, #2                         // =2
	mov	x9, x25
	b.lo	.LBB119_15
// %bb.12:                              //   in Loop: Header=BB119_9 Depth=2
	and	x11, x10, #0xfffffffffffffffe
	add	x12, x19, x8
	add	x9, x25, x11
	and	x12, x12, #0xfffffffffffffffe
	mov	x13, x20
.LBB119_13:                             //   Parent Loop BB119_5 Depth=1
                                        //     Parent Loop BB119_9 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	ldrh	w14, [x13]
	ldrh	w15, [x13, #2]
	subs	x12, x12, #2                    // =2
	sturh	w14, [x13, #-2]
	strh	w15, [x13], #4
	b.ne	.LBB119_13
// %bb.14:                              //   in Loop: Header=BB119_9 Depth=2
	cmp	x10, x11
	b.eq	.LBB119_7
.LBB119_15:                             //   Parent Loop BB119_5 Depth=1
                                        //     Parent Loop BB119_9 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	add	x10, x24, x9, lsl #1
	ldrh	w11, [x10, #2]
	add	x9, x9, #1                      // =1
	cmp	x9, x8
	strh	w11, [x10]
	b.lo	.LBB119_15
	b	.LBB119_7
.LBB119_16:                             //   in Loop: Header=BB119_5 Depth=1
	mov	w10, wzr
	b	.LBB119_4
.LBB119_17:
	tst	w8, #0xffff
	b.eq	.LBB119_28
// %bb.18:
	adrp	x21, g_gc_next_blk
	ldrh	w9, [x21, :lo12:g_gc_next_blk]
	mov	w10, #65535
	cmp	w9, w10
	b.ne	.LBB119_28
// %bb.19:
	adrp	x20, g_gc_bad_block_gc_index
	ldrh	w9, [x20, :lo12:g_gc_bad_block_gc_index]
	cmp	w9, w8, uxth
	b.lo	.LBB119_21
// %bb.20:
	mov	w9, wzr
	strh	wzr, [x20, :lo12:g_gc_bad_block_gc_index]
.LBB119_21:
	ldrh	w8, [x24, w9, uxtw #1]
	adrp	x9, c_ftl_nand_blks_per_die
	ldrh	w9, [x9, :lo12:c_ftl_nand_blks_per_die]
	adrp	x10, c_ftl_nand_planes_per_die
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_per_die]
	adrp	x0, .L.str.112
	udiv	w11, w8, w9
	msub	w8, w11, w9, w8
	udiv	w19, w8, w10
	add	x0, x0, :lo12:.L.str.112
	mov	w1, w19
	bl	sftl_printk
	ldrh	w9, [x21, :lo12:g_gc_next_blk]
	cmp	w9, w19
	b.eq	.LBB119_27
// %bb.22:
	adrp	x8, g_gc_next_blk_1
	ldrh	w10, [x8, :lo12:g_gc_next_blk_1]
	cmp	w10, w19, uxth
	b.eq	.LBB119_27
// %bb.23:
	mov	w11, #65535
	cmp	w9, w11
	b.eq	.LBB119_26
// %bb.24:
	cmp	w10, w11
	b.ne	.LBB119_27
// %bb.25:
	strh	w19, [x8, :lo12:g_gc_next_blk_1]
	b	.LBB119_27
.LBB119_26:
	strh	w19, [x21, :lo12:g_gc_next_blk]
.LBB119_27:
	ldrh	w8, [x20, :lo12:g_gc_bad_block_gc_index]
	add	w8, w8, #1                      // =1
	strh	w8, [x20, :lo12:g_gc_bad_block_gc_index]
.LBB119_28:
	ldp	x20, x19, [sp, #96]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #80]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #64]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #48]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	mov	w0, wzr
	add	sp, sp, #112                    // =112
	hint	#29
	ret
.Lfunc_end119:
	.size	FtlGcFreeBadSuperBlk, .Lfunc_end119-FtlGcFreeBadSuperBlk
                                        // -- End function
	.globl	FtlGcBufFree                    // -- Begin function FtlGcBufFree
	.p2align	2
	.type	FtlGcBufFree,@function
FtlGcBufFree:                           // @FtlGcBufFree
// %bb.0:
	hint	#25
	cbz	w1, .LBB120_8
// %bb.1:
	mov	w8, wzr
	adrp	x9, c_gc_page_buf_num
	adrp	x10, gp_gc_page_buf_info
	mov	w11, #24
	b	.LBB120_4
.LBB120_2:                              //   in Loop: Header=BB120_4 Depth=1
                                        // kill: def $w15 killed $w15 killed $x15 def $x15
	and	x13, x15, #0xffff
	madd	x12, x13, x11, x12
	str	wzr, [x12, #16]
.LBB120_3:                              //   in Loop: Header=BB120_4 Depth=1
	add	w8, w8, #1                      // =1
	cmp	w1, w8, uxth
	b.ls	.LBB120_8
.LBB120_4:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB120_6 Depth 2
	ldr	w13, [x9, :lo12:c_gc_page_buf_num]
	cbz	w13, .LBB120_3
// %bb.5:                               //   in Loop: Header=BB120_4 Depth=1
	and	x14, x8, #0xffff
	add	x14, x0, x14, lsl #5
	ldr	x12, [x10, :lo12:gp_gc_page_buf_info]
	ldr	x14, [x14, #8]
	mov	w15, wzr
.LBB120_6:                              //   Parent Loop BB120_4 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	and	x16, x15, #0xffff
	mul	x16, x16, x11
	ldr	x16, [x12, x16]
	cmp	x16, x14
	b.eq	.LBB120_2
// %bb.7:                               //   in Loop: Header=BB120_6 Depth=2
	add	w15, w15, #1                    // =1
	cmp	w13, w15, uxth
	b.hi	.LBB120_6
	b	.LBB120_3
.LBB120_8:
	hint	#29
	ret
.Lfunc_end120:
	.size	FtlGcBufFree, .Lfunc_end120-FtlGcBufFree
                                        // -- End function
	.globl	FtlGcBufAlloc                   // -- Begin function FtlGcBufAlloc
	.p2align	2
	.type	FtlGcBufAlloc,@function
FtlGcBufAlloc:                          // @FtlGcBufAlloc
// %bb.0:
	hint	#25
	cbz	w1, .LBB121_8
// %bb.1:
	mov	w8, wzr
	adrp	x9, c_gc_page_buf_num
	adrp	x10, gp_gc_page_buf_info
	mov	w11, #24
	mov	w12, #1
	b	.LBB121_4
.LBB121_2:                              //   in Loop: Header=BB121_4 Depth=1
	str	w12, [x16]
	ldr	x13, [x10, :lo12:gp_gc_page_buf_info]
                                        // kill: def $w15 killed $w15 killed $x15 def $x15
	and	x14, x15, #0xffff
	add	x14, x14, w15, uxth #1
	lsl	x14, x14, #3
	ldr	x13, [x13, x14]
	and	x15, x8, #0xffff
	add	x15, x0, x15, lsl #5
	str	x13, [x15, #8]
	ldr	x13, [x10, :lo12:gp_gc_page_buf_info]
	add	x13, x13, x14
	ldr	x13, [x13, #8]
	str	x13, [x15, #16]
.LBB121_3:                              //   in Loop: Header=BB121_4 Depth=1
	add	w8, w8, #1                      // =1
	cmp	w1, w8, uxth
	b.ls	.LBB121_8
.LBB121_4:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB121_6 Depth 2
	ldr	w13, [x9, :lo12:c_gc_page_buf_num]
	cbz	w13, .LBB121_3
// %bb.5:                               //   in Loop: Header=BB121_4 Depth=1
	ldr	x14, [x10, :lo12:gp_gc_page_buf_info]
	mov	w15, wzr
.LBB121_6:                              //   Parent Loop BB121_4 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	and	x16, x15, #0xffff
	madd	x16, x16, x11, x14
	ldr	w17, [x16, #16]!
	cbz	w17, .LBB121_2
// %bb.7:                               //   in Loop: Header=BB121_6 Depth=2
	add	w15, w15, #1                    // =1
	cmp	w13, w15, uxth
	b.hi	.LBB121_6
	b	.LBB121_3
.LBB121_8:
	hint	#29
	ret
.Lfunc_end121:
	.size	FtlGcBufAlloc, .Lfunc_end121-FtlGcBufAlloc
                                        // -- End function
	.globl	IsBlkInGcList                   // -- Begin function IsBlkInGcList
	.p2align	2
	.type	IsBlkInGcList,@function
IsBlkInGcList:                          // @IsBlkInGcList
// %bb.0:
	hint	#25
	adrp	x8, g_gc_blk_num
	ldrh	w8, [x8, :lo12:g_gc_blk_num]
	cbz	x8, .LBB122_4
// %bb.1:
	adrp	x9, p_gc_blk_tbl
	ldr	x9, [x9, :lo12:p_gc_blk_tbl]
.LBB122_2:                              // =>This Inner Loop Header: Depth=1
	ldrh	w10, [x9]
	cmp	w10, w0, uxth
	b.eq	.LBB122_5
// %bb.3:                               //   in Loop: Header=BB122_2 Depth=1
	subs	x8, x8, #1                      // =1
	add	x9, x9, #2                      // =2
	b.ne	.LBB122_2
.LBB122_4:
	mov	w0, wzr
	hint	#29
	ret
.LBB122_5:
	mov	w0, #1
	hint	#29
	ret
.Lfunc_end122:
	.size	IsBlkInGcList, .Lfunc_end122-IsBlkInGcList
                                        // -- End function
	.globl	FtlGcUpdatePage                 // -- Begin function FtlGcUpdatePage
	.p2align	2
	.type	FtlGcUpdatePage,@function
FtlGcUpdatePage:                        // @FtlGcUpdatePage
// %bb.0:
	hint	#25
	adrp	x8, c_ftl_nand_blks_per_die
	ldrh	w12, [x8, :lo12:c_ftl_nand_blks_per_die]
	adrp	x10, c_ftl_nand_planes_per_die
	adrp	x8, g_gc_blk_num
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_per_die]
	ubfx	w9, w0, #10, #16
	ldrh	w11, [x8, :lo12:g_gc_blk_num]
	udiv	w13, w9, w12
	msub	w9, w13, w12, w9
	udiv	w9, w9, w10
	adrp	x10, p_gc_blk_tbl
	cbz	x11, .LBB123_5
// %bb.1:
	ldr	x12, [x10, :lo12:p_gc_blk_tbl]
	mov	x13, xzr
.LBB123_2:                              // =>This Inner Loop Header: Depth=1
	ldrh	w14, [x12, x13, lsl #1]
	cmp	w14, w9, uxth
	b.eq	.LBB123_4
// %bb.3:                               //   in Loop: Header=BB123_2 Depth=1
	add	x13, x13, #1                    // =1
	cmp	x11, x13
	b.ne	.LBB123_2
	b	.LBB123_5
.LBB123_4:
	cmp	w11, w13, uxth
	mov	w11, w13
	b.ne	.LBB123_6
.LBB123_5:
	ldr	x10, [x10, :lo12:p_gc_blk_tbl]
                                        // kill: def $w11 killed $w11 killed $x11 def $x11
	and	x11, x11, #0xffff
	strh	w9, [x10, x11, lsl #1]
	ldrh	w9, [x8, :lo12:g_gc_blk_num]
	add	w9, w9, #1                      // =1
	strh	w9, [x8, :lo12:g_gc_blk_num]
.LBB123_6:
	adrp	x8, p_gc_page_info
	adrp	x9, g_gc_page_offset
	ldr	x10, [x8, :lo12:p_gc_page_info]
	ldrh	w11, [x9, :lo12:g_gc_page_offset]
	mov	w12, #12
	madd	x10, x11, x12, x10
	str	w1, [x10, #4]
	ldr	x10, [x8, :lo12:p_gc_page_info]
	ldrh	w11, [x9, :lo12:g_gc_page_offset]
	madd	x10, x11, x12, x10
	str	w2, [x10, #8]
	ldrh	w10, [x9, :lo12:g_gc_page_offset]
	ldr	x8, [x8, :lo12:p_gc_page_info]
	mul	x10, x10, x12
	str	w0, [x8, x10]
	ldrh	w8, [x9, :lo12:g_gc_page_offset]
	add	w8, w8, #1                      // =1
	strh	w8, [x9, :lo12:g_gc_page_offset]
	hint	#29
	ret
.Lfunc_end123:
	.size	FtlGcUpdatePage, .Lfunc_end123-FtlGcUpdatePage
                                        // -- End function
	.globl	FtlGcFreeTempBlock              // -- Begin function FtlGcFreeTempBlock
	.p2align	2
	.type	FtlGcFreeTempBlock,@function
FtlGcFreeTempBlock:                     // @FtlGcFreeTempBlock
// %bb.0:
	hint	#25
	sub	sp, sp, #112                    // =112
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	stp	x28, x27, [sp, #32]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #48]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #64]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #80]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #96]             // 16-byte Folded Spill
	str	x8, [sp, #8]
	adrp	x20, g_gc_temp_superblock
	ldrsh	w8, [x20, :lo12:g_gc_temp_superblock]
	add	x29, sp, #16                    // =16
	cmn	w8, #1                          // =1
	b.eq	.LBB124_4
// %bb.1:
	adrp	x22, c_ftl_nand_page_pre_blk
	ldrh	w8, [x22, :lo12:c_ftl_nand_page_pre_blk]
	adrp	x21, ftl_gc_temp_block_bops_scan_page_addr
	cbz	w0, .LBB124_5
// %bb.2:
	ldrsh	w9, [x21, :lo12:ftl_gc_temp_block_bops_scan_page_addr]
	mov	w1, #2
	cmn	w9, #1                          // =1
	b.ne	.LBB124_6
// %bb.3:
	adrp	x9, g_num_free_superblocks
	ldrh	w9, [x9, :lo12:g_num_free_superblocks]
	strh	wzr, [x21, :lo12:ftl_gc_temp_block_bops_scan_page_addr]
	cmp	w9, #18                         // =18
	csel	w1, w8, w1, lo
	b	.LBB124_6
.LBB124_4:
	adrp	x8, ftl_gc_temp_power_lost_recovery_flag
	mov	w0, wzr
	str	wzr, [x8, :lo12:ftl_gc_temp_power_lost_recovery_flag]
	b	.LBB124_8
.LBB124_5:
	mov	w1, w8
.LBB124_6:
	adrp	x19, g_gc_temp_superblock
	add	x19, x19, :lo12:g_gc_temp_superblock
	mov	x0, x19
	bl	FtlGcScanTempBlk
	ldrsh	w8, [x21, :lo12:ftl_gc_temp_block_bops_scan_page_addr]
	mov	w9, #-1
	str	w9, [sp, #4]
	cmn	w8, #1                          // =1
	b.eq	.LBB124_10
// %bb.7:
	mov	w0, #1
.LBB124_8:
	adrp	x9, __stack_chk_guard
	ldr	x8, [sp, #8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB124_39
// %bb.9:
	ldp	x20, x19, [sp, #96]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #80]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #64]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #48]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	add	sp, sp, #112                    // =112
	hint	#29
	ret
.LBB124_10:
	ldrh	w8, [x20, :lo12:g_gc_temp_superblock]
	mov	w10, #65535
	adrp	x9, ftl_gc_temp_power_lost_recovery_flag
	str	wzr, [x9, :lo12:ftl_gc_temp_power_lost_recovery_flag]
	cmp	w8, w10
	b.eq	.LBB124_26
// %bb.11:
	adrp	x9, g_gc_temp_superblock+7
	adrp	x21, g_gc_page_offset
	ldrb	w9, [x9, :lo12:g_gc_temp_superblock+7]
	ldrh	w10, [x22, :lo12:c_ftl_nand_page_pre_blk]
	ldrh	w11, [x21, :lo12:g_gc_page_offset]
	mul	w12, w10, w9
	cmp	w12, w11
	b.eq	.LBB124_13
// %bb.12:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.FtlGcFreeTempBlock
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.FtlGcFreeTempBlock
	mov	w2, #164
	bl	sftl_printk
	ldrb	w9, [x19, #7]
	ldrh	w10, [x22, :lo12:c_ftl_nand_page_pre_blk]
	ldrh	w8, [x19]
.LBB124_13:
	adrp	x11, p_valid_page_count_table
	ldr	x11, [x11, :lo12:p_valid_page_count_table]
	mul	w9, w10, w9
	adrp	x10, g_totle_gc_page_count
	strh	w9, [x11, w8, uxtw #1]
	ldrh	w8, [x21, :lo12:g_gc_page_offset]
	ldr	w9, [x10, :lo12:g_totle_gc_page_count]
	add	w9, w9, w8
	str	w9, [x10, :lo12:g_totle_gc_page_count]
	cbz	w8, .LBB124_22
// %bb.14:
	mov	x23, xzr
	mov	x24, xzr
	adrp	x25, p_gc_page_info
	adrp	x26, g_MaxLpn
	adrp	x27, c_ftl_nand_blks_per_die
	adrp	x28, c_ftl_nand_planes_per_die
	b	.LBB124_18
.LBB124_15:                             //   in Loop: Header=BB124_18 Depth=1
	ldrh	w19, [x20, :lo12:g_gc_temp_superblock]
.LBB124_16:                             //   in Loop: Header=BB124_18 Depth=1
	mov	w0, w19
	bl	decrement_vpc_count
.LBB124_17:                             //   in Loop: Header=BB124_18 Depth=1
	ldrh	w8, [x21, :lo12:g_gc_page_offset]
	add	x24, x24, #1                    // =1
	add	x23, x23, #12                   // =12
	cmp	x24, x8
	b.hs	.LBB124_22
.LBB124_18:                             // =>This Inner Loop Header: Depth=1
	ldr	x19, [x25, :lo12:p_gc_page_info]
	ldr	w8, [x26, :lo12:g_MaxLpn]
	add	x22, x19, x23
	ldr	w0, [x22, #8]
	cmp	w0, w8
	b.hs	.LBB124_15
// %bb.19:                              //   in Loop: Header=BB124_18 Depth=1
	add	x1, sp, #4                      // =4
	mov	w2, wzr
	bl	log2phys
	ldr	w8, [sp, #4]
	ldr	w9, [x19, x23]
	cmp	w8, w9
	b.ne	.LBB124_21
// %bb.20:                              //   in Loop: Header=BB124_18 Depth=1
	ldrh	w9, [x27, :lo12:c_ftl_nand_blks_per_die]
	ldrh	w10, [x28, :lo12:c_ftl_nand_planes_per_die]
	ldr	w0, [x22, #8]
	ubfx	w8, w8, #10, #16
	add	x11, x19, x23
	udiv	w12, w8, w9
	msub	w8, w12, w9, w8
	add	x1, x11, #4                     // =4
	mov	w2, #1
	udiv	w19, w8, w10
	bl	log2phys
	b	.LBB124_16
.LBB124_21:                             //   in Loop: Header=BB124_18 Depth=1
	add	x9, x19, x23
	ldr	w9, [x9, #4]
	cmp	w8, w9
	b.ne	.LBB124_15
	b	.LBB124_17
.LBB124_22:
	adrp	x19, g_tmp_data_superblock_id
	ldrh	w0, [x19, :lo12:g_tmp_data_superblock_id]
	mov	w22, #65535
	cmp	w0, w22
	b.eq	.LBB124_27
// %bb.23:
	bl	update_vpc_list
	adrp	x8, p_data_block_list_head
	adrp	x9, p_data_block_list_table
	ldr	w8, [x8, :lo12:p_data_block_list_head]
	ldr	w9, [x9, :lo12:p_data_block_list_table]
	strh	w22, [x19, :lo12:g_tmp_data_superblock_id]
	adrp	x19, p_valid_page_count_table
	ldr	x10, [x19, :lo12:p_valid_page_count_table]
	sub	w8, w8, w9
	mov	w9, #43691
	lsr	w8, w8, #1
	movk	w9, #43690, lsl #16
	mul	w8, w8, w9
	and	x8, x8, #0xffff
	ldrh	w9, [x10, x8, lsl #1]
	cbnz	w9, .LBB124_28
// %bb.24:
	mov	w9, #65535
	cmp	x8, x9
	b.eq	.LBB124_28
// %bb.25:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.decrement_vpc_count
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.decrement_vpc_count
	mov	w2, #2869
	bl	sftl_printk
	b	.LBB124_28
.LBB124_26:
	mov	w0, wzr
	b	.LBB124_8
.LBB124_27:
	strh	w22, [x19, :lo12:g_tmp_data_superblock_id]
	adrp	x19, p_valid_page_count_table
.LBB124_28:
	ldr	x8, [x19, :lo12:p_valid_page_count_table]
	ldrh	w0, [x20, :lo12:g_gc_temp_superblock]
	ldrh	w8, [x8, x0, lsl #1]
	cbz	w8, .LBB124_31
// %bb.29:
                                        // kill: def $w0 killed $w0 killed $x0
	bl	insert_data_list
	adrp	x8, g_num_data_superblocks
	ldrh	w9, [x8, :lo12:g_num_data_superblocks]
	adrp	x10, c_ftl_nand_data_blks_per_plane
	ldrh	w10, [x10, :lo12:c_ftl_nand_data_blks_per_plane]
	add	w9, w9, #1                      // =1
	strh	w9, [x8, :lo12:g_num_data_superblocks]
	cmp	w10, w9, uxth
	b.hs	.LBB124_32
// %bb.30:
	adrp	x0, .L.str.78
	adrp	x1, .L__func__.INSERT_DATA_LIST
	add	x0, x0, :lo12:.L.str.78
	add	x1, x1, :lo12:.L__func__.INSERT_DATA_LIST
	mov	w2, #214
	bl	sftl_printk
	b	.LBB124_32
.LBB124_31:
                                        // kill: def $w0 killed $w0 killed $x0
	bl	INSERT_FREE_LIST
.LBB124_32:
	mov	w22, #65535
	adrp	x8, g_gc_blk_num
	strh	wzr, [x21, :lo12:g_gc_page_offset]
	strh	w22, [x20, :lo12:g_gc_temp_superblock]
	strh	wzr, [x8, :lo12:g_gc_blk_num]
	bl	FtlWriteDump_data
	adrp	x20, c_ftl_nand_l2pmap_ram_region_num
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	cbz	w8, .LBB124_37
// %bb.33:
	adrp	x19, gL2pMapInfo
	mov	x21, xzr
	mov	x23, xzr
	adrp	x24, p_l2p_ram_map
	add	x19, x19, :lo12:gL2pMapInfo
	b	.LBB124_35
.LBB124_34:                             //   in Loop: Header=BB124_35 Depth=1
	add	x23, x23, #1                    // =1
	cmp	x23, w8, uxth
	add	x21, x21, #16                   // =16
	b.hs	.LBB124_37
.LBB124_35:                             // =>This Inner Loop Header: Depth=1
	ldr	x9, [x24, :lo12:p_l2p_ram_map]
	add	x9, x9, x21
	ldr	w10, [x9, #4]
	tbz	w10, #31, .LBB124_34
// %bb.36:                              //   in Loop: Header=BB124_35 Depth=1
	ldrh	w1, [x9]
	ldr	x2, [x9, #8]
	mov	x0, x19
	bl	FtlMapWritePage
	ldr	x8, [x24, :lo12:p_l2p_ram_map]
	add	x8, x8, x21
	ldr	w9, [x8, #4]
	and	w9, w9, #0x7fffffff
	str	w9, [x8, #4]
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	b	.LBB124_34
.LBB124_37:
	bl	FtlVpcTblFlush
	adrp	x9, c_ftl_nand_data_op_blks_per_plane
	adrp	x8, g_num_free_superblocks
	ldrh	w9, [x9, :lo12:c_ftl_nand_data_op_blks_per_plane]
	ldrh	w8, [x8, :lo12:g_num_free_superblocks]
	adrp	x10, g_gc_superblock
	mov	w0, wzr
	add	w9, w9, w9, lsl #1
	cmp	w8, w9, lsr #2
	strh	w22, [x10, :lo12:g_gc_superblock]
	b.ls	.LBB124_8
// %bb.38:
	adrp	x8, g_gc_free_blk_threshold
	mov	w9, #20
	strh	w9, [x8, :lo12:g_gc_free_blk_threshold]
	b	.LBB124_8
.LBB124_39:
	bl	__stack_chk_fail
.Lfunc_end124:
	.size	FtlGcFreeTempBlock, .Lfunc_end124-FtlGcFreeTempBlock
                                        // -- End function
	.globl	FtlGcScanTempBlk                // -- Begin function FtlGcScanTempBlk
	.p2align	2
	.type	FtlGcScanTempBlk,@function
FtlGcScanTempBlk:                       // @FtlGcScanTempBlk
// %bb.0:
	hint	#25
	sub	sp, sp, #176                    // =176
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	stp	x29, x30, [sp, #80]             // 16-byte Folded Spill
	add	x29, sp, #80                    // =80
	stp	x28, x27, [sp, #96]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #112]            // 16-byte Folded Spill
	stp	x24, x23, [sp, #128]            // 16-byte Folded Spill
	stp	x22, x21, [sp, #144]            // 16-byte Folded Spill
	stp	x20, x19, [sp, #160]            // 16-byte Folded Spill
	str	w1, [sp, #12]                   // 4-byte Folded Spill
	adrp	x10, ftl_gc_temp_block_bops_scan_page_addr
	stur	x8, [x29, #-8]
	ldrh	w8, [x10, :lo12:ftl_gc_temp_block_bops_scan_page_addr]
	mov	w9, #65535
	mov	x22, x0
	adrp	x11, g_gc_blk_num
	cmp	w8, w9
	adrp	x26, g_gc_page_offset
	csel	w19, wzr, w8, eq
	adrp	x24, p_gc_page_info
	cbz	w8, .LBB125_2
// %bb.1:
	adrp	x8, c_ftl_nand_page_pre_blk
	ldrh	w8, [x8, :lo12:c_ftl_nand_page_pre_blk]
	ldr	w9, [sp, #12]                   // 4-byte Folded Reload
	cmp	w8, w9
	b.ne	.LBB125_3
.LBB125_2:
	adrp	x20, c_ftl_nand_page_pre_super_blk
	ldrh	w8, [x20, :lo12:c_ftl_nand_page_pre_super_blk]
	adrp	x9, p_gc_blk_tbl
	ldr	x0, [x9, :lo12:p_gc_blk_tbl]
	mov	w1, #255
	lsl	x2, x8, #1
	strh	wzr, [x11, :lo12:g_gc_blk_num]
	strh	wzr, [x26, :lo12:g_gc_page_offset]
	bl	memset
	ldrh	w8, [x20, :lo12:c_ftl_nand_page_pre_super_blk]
	ldr	x0, [x24, :lo12:p_gc_page_info]
	mov	w1, #255
	add	x8, x8, x8, lsl #1
	lsl	x2, x8, #2
	bl	memset
	bl	FtlGcBufInit
.LBB125_3:
	ldrsh	w8, [x22]
	adrp	x16, c_ftl_nand_planes_num
	strb	wzr, [x22, #8]
	cmn	w8, #1                          // =1
	b.eq	.LBB125_44
// %bb.4:
	add	x20, x22, #16                   // =16
	adrp	x28, req_read
	mov	w21, #65535
	mov	w27, #12
	adrp	x17, p_gc_data_buf
	adrp	x18, c_ftl_nand_byte_pre_page
	adrp	x0, p_gc_spare_buf
	adrp	x1, c_ftl_nand_byte_pre_oob
	str	wzr, [sp, #16]                  // 4-byte Folded Spill
	str	x22, [sp, #24]                  // 8-byte Folded Spill
	b	.LBB125_6
.LBB125_5:                              //   in Loop: Header=BB125_6 Depth=1
	adrp	x8, c_ftl_nand_page_pre_blk
	ldrh	w8, [x8, :lo12:c_ftl_nand_page_pre_blk]
	adrp	x16, c_ftl_nand_planes_num
	adrp	x17, p_gc_data_buf
	adrp	x18, c_ftl_nand_byte_pre_page
	cmp	w8, w19, uxth
	adrp	x0, p_gc_spare_buf
	adrp	x1, c_ftl_nand_byte_pre_oob
	str	w10, [sp, #16]                  // 4-byte Folded Spill
	b.ls	.LBB125_44
.LBB125_6:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB125_7 Depth 2
                                        //       Child Loop BB125_10 Depth 3
                                        //       Child Loop BB125_18 Depth 3
                                        //         Child Loop BB125_21 Depth 4
                                        //         Child Loop BB125_29 Depth 4
                                        //         Child Loop BB125_33 Depth 4
	mov	w9, w19
.LBB125_7:                              //   Parent Loop BB125_6 Depth=1
                                        // =>  This Loop Header: Depth=2
                                        //       Child Loop BB125_10 Depth 3
                                        //       Child Loop BB125_18 Depth 3
                                        //         Child Loop BB125_21 Depth 4
                                        //         Child Loop BB125_29 Depth 4
                                        //         Child Loop BB125_33 Depth 4
	ldrh	w10, [x16, :lo12:c_ftl_nand_planes_num]
	str	w9, [sp, #20]                   // 4-byte Folded Spill
	cbz	w10, .LBB125_12
// %bb.8:                               //   in Loop: Header=BB125_7 Depth=2
	mov	x8, xzr
	mov	w19, wzr
	and	w9, w9, #0xffff
	b	.LBB125_10
.LBB125_9:                              //   in Loop: Header=BB125_10 Depth=3
	add	x8, x8, #1                      // =1
	cmp	x8, w10, uxth
	b.hs	.LBB125_13
.LBB125_10:                             //   Parent Loop BB125_6 Depth=1
                                        //     Parent Loop BB125_7 Depth=2
                                        // =>    This Inner Loop Header: Depth=3
	ldrh	w11, [x20, x8, lsl #1]
	cmp	w11, w21
	b.eq	.LBB125_9
// %bb.11:                              //   in Loop: Header=BB125_10 Depth=3
	ldr	x10, [x28, :lo12:req_read]
	and	x12, x19, #0xffff
	lsl	x12, x12, #5
	orr	w11, w9, w11, lsl #10
	add	x10, x10, x12
	str	w11, [x10, #4]
	ldrh	w10, [x18, :lo12:c_ftl_nand_byte_pre_page]
	and	w11, w19, #0xffff
	ldr	x13, [x17, :lo12:p_gc_data_buf]
	ldr	x14, [x28, :lo12:req_read]
	mul	w10, w10, w11
	add	w15, w10, #3                    // =3
	cmp	w10, #0                         // =0
	csel	w10, w15, w10, lt
	asr	w10, w10, #2
	add	x14, x14, x12
	add	x10, x13, w10, sxtw #2
	str	x10, [x14, #8]
	ldrh	w10, [x1, :lo12:c_ftl_nand_byte_pre_oob]
	ldr	x13, [x0, :lo12:p_gc_spare_buf]
	ldr	x14, [x28, :lo12:req_read]
	add	w19, w19, #1                    // =1
	mul	w10, w10, w11
	add	w11, w10, #3                    // =3
	cmp	w10, #0                         // =0
	csel	w10, w11, w10, lt
	asr	w10, w10, #2
	add	x10, x13, w10, sxtw #2
	add	x11, x14, x12
	str	x10, [x11, #16]
	ldrh	w10, [x16, :lo12:c_ftl_nand_planes_num]
	b	.LBB125_9
.LBB125_12:                             //   in Loop: Header=BB125_7 Depth=2
	mov	w19, wzr
.LBB125_13:                             //   in Loop: Header=BB125_7 Depth=2
	ldr	x0, [x28, :lo12:req_read]
	and	w22, w19, #0xffff
	mov	w1, w22
	bl	FlashReadPages
	adrp	x16, g_gc_blk_num
	cbz	w22, .LBB125_41
// %bb.14:                              //   in Loop: Header=BB125_7 Depth=2
                                        // kill: def $w19 killed $w19 killed $x19 def $x19
	mov	x22, xzr
	and	x19, x19, #0xffff
	b	.LBB125_18
.LBB125_15:                             //   in Loop: Header=BB125_18 Depth=3
	strh	w11, [x10, x9, lsl #1]
	ldrh	w9, [x16, :lo12:g_gc_blk_num]
	add	w9, w9, #1                      // =1
	strh	w9, [x16, :lo12:g_gc_blk_num]
.LBB125_16:                             //   in Loop: Header=BB125_18 Depth=3
	ldr	x9, [x24, :lo12:p_gc_page_info]
	ldrh	w10, [x26, :lo12:g_gc_page_offset]
	madd	x9, x10, x27, x9
	str	w23, [x9, #4]
	ldr	x9, [x24, :lo12:p_gc_page_info]
	ldrh	w10, [x26, :lo12:g_gc_page_offset]
	madd	x9, x10, x27, x9
	mov	w10, #-1
	str	w10, [x9, #8]
.LBB125_17:                             //   in Loop: Header=BB125_18 Depth=3
	ldrh	w9, [x26, :lo12:g_gc_page_offset]
	ldr	x10, [x24, :lo12:p_gc_page_info]
	add	x22, x22, #1                    // =1
	cmp	x22, x19
	mul	x9, x9, x27
	str	w8, [x10, x9]
	ldrh	w8, [x26, :lo12:g_gc_page_offset]
	add	w8, w8, #1                      // =1
	strh	w8, [x26, :lo12:g_gc_page_offset]
	b.eq	.LBB125_41
.LBB125_18:                             //   Parent Loop BB125_6 Depth=1
                                        //     Parent Loop BB125_7 Depth=2
                                        // =>    This Loop Header: Depth=3
                                        //         Child Loop BB125_21 Depth 4
                                        //         Child Loop BB125_29 Depth 4
                                        //         Child Loop BB125_33 Depth 4
	ldr	x8, [x28, :lo12:req_read]
	add	x8, x8, x22, lsl #5
	ldp	w9, w23, [x8]
	ldr	x25, [x8, #16]
	cbz	w9, .LBB125_23
// %bb.19:                              //   in Loop: Header=BB125_18 Depth=3
	ldr	w8, [x25, #12]
	adrp	x9, c_ftl_nand_blks_per_die
	ldrh	w11, [x9, :lo12:c_ftl_nand_blks_per_die]
	adrp	x9, c_ftl_nand_planes_per_die
	ldrh	w12, [x9, :lo12:c_ftl_nand_planes_per_die]
	adrp	x10, p_gc_blk_tbl
	ldrh	w9, [x16, :lo12:g_gc_blk_num]
	ubfx	w13, w8, #10, #16
	ldr	x10, [x10, :lo12:p_gc_blk_tbl]
	udiv	w14, w13, w11
	msub	w11, w14, w11, w13
	udiv	w11, w11, w12
	cbz	x9, .LBB125_15
// %bb.20:                              //   in Loop: Header=BB125_18 Depth=3
	mov	x12, xzr
.LBB125_21:                             //   Parent Loop BB125_6 Depth=1
                                        //     Parent Loop BB125_7 Depth=2
                                        //       Parent Loop BB125_18 Depth=3
                                        // =>      This Inner Loop Header: Depth=4
	ldrh	w13, [x10, x12, lsl #1]
	cmp	w13, w11, uxth
	b.eq	.LBB125_35
// %bb.22:                              //   in Loop: Header=BB125_21 Depth=4
	add	x12, x12, #1                    // =1
	cmp	x9, x12
	b.ne	.LBB125_21
	b	.LBB125_15
.LBB125_23:                             //   in Loop: Header=BB125_18 Depth=3
	ldrsh	w8, [x25]
	cmn	w8, #1                          // =1
	b.eq	.LBB125_40
// %bb.24:                              //   in Loop: Header=BB125_18 Depth=3
	adrp	x8, g_MaxLpn
	ldr	w0, [x25, #8]
	ldr	w8, [x8, :lo12:g_MaxLpn]
	cmp	w0, w8
	b.hi	.LBB125_40
// %bb.25:                              //   in Loop: Header=BB125_18 Depth=3
	add	x1, sp, #36                     // =36
	mov	w2, wzr
	bl	log2phys
	ldr	w8, [sp, #36]
	adrp	x16, g_gc_blk_num
	cmn	w8, #1                          // =1
	b.eq	.LBB125_31
// %bb.26:                              //   in Loop: Header=BB125_18 Depth=3
	ldr	w9, [x25, #12]
	cmp	w8, w9
	b.ne	.LBB125_31
// %bb.27:                              //   in Loop: Header=BB125_18 Depth=3
	adrp	x9, p_io_data_buf_1
	adrp	x10, p_io_spare_buf
	ldr	x9, [x9, :lo12:p_io_data_buf_1]
	ldr	x10, [x10, :lo12:p_io_spare_buf]
	add	x0, sp, #40                     // =40
	mov	w1, #1
	str	w8, [sp, #44]
	stp	x9, x10, [sp, #48]
	bl	FlashReadPages
	adrp	x8, c_ftl_nand_sec_pre_page
	ldrh	w10, [x8, :lo12:c_ftl_nand_sec_pre_page]
	adrp	x16, g_gc_blk_num
	cbz	x10, .LBB125_31
// %bb.28:                              //   in Loop: Header=BB125_18 Depth=3
	ldr	x8, [x28, :lo12:req_read]
	ldr	x9, [sp, #48]
	lsl	x10, x10, #7
	add	x8, x8, x22, lsl #5
	ldr	x8, [x8, #8]
.LBB125_29:                             //   Parent Loop BB125_6 Depth=1
                                        //     Parent Loop BB125_7 Depth=2
                                        //       Parent Loop BB125_18 Depth=3
                                        // =>      This Inner Loop Header: Depth=4
	ldr	w11, [x8]
	ldr	w12, [x9]
	cmp	w11, w12
	b.ne	.LBB125_39
// %bb.30:                              //   in Loop: Header=BB125_29 Depth=4
	add	x8, x8, #4                      // =4
	subs	x10, x10, #1                    // =1
	add	x9, x9, #4                      // =4
	b.ne	.LBB125_29
.LBB125_31:                             //   in Loop: Header=BB125_18 Depth=3
	ldp	w9, w8, [x25, #8]
	adrp	x10, c_ftl_nand_blks_per_die
	ldrh	w12, [x10, :lo12:c_ftl_nand_blks_per_die]
	adrp	x10, c_ftl_nand_planes_per_die
	ldrh	w13, [x10, :lo12:c_ftl_nand_planes_per_die]
	adrp	x11, p_gc_blk_tbl
	ldrh	w10, [x16, :lo12:g_gc_blk_num]
	ubfx	w14, w8, #10, #16
	ldr	x11, [x11, :lo12:p_gc_blk_tbl]
	udiv	w15, w14, w12
	msub	w12, w15, w12, w14
	udiv	w12, w12, w13
	cbz	x10, .LBB125_37
// %bb.32:                              //   in Loop: Header=BB125_18 Depth=3
	mov	x13, xzr
.LBB125_33:                             //   Parent Loop BB125_6 Depth=1
                                        //     Parent Loop BB125_7 Depth=2
                                        //       Parent Loop BB125_18 Depth=3
                                        // =>      This Inner Loop Header: Depth=4
	ldrh	w14, [x11, x13, lsl #1]
	cmp	w14, w12, uxth
	b.eq	.LBB125_36
// %bb.34:                              //   in Loop: Header=BB125_33 Depth=4
	add	x13, x13, #1                    // =1
	cmp	x10, x13
	b.ne	.LBB125_33
	b	.LBB125_37
.LBB125_35:                             //   in Loop: Header=BB125_18 Depth=3
	cmp	w9, w12, uxth
	b.eq	.LBB125_15
	b	.LBB125_16
.LBB125_36:                             //   in Loop: Header=BB125_18 Depth=3
	cmp	w10, w13, uxth
	b.ne	.LBB125_38
.LBB125_37:                             //   in Loop: Header=BB125_18 Depth=3
	strh	w12, [x11, x10, lsl #1]
	ldrh	w10, [x16, :lo12:g_gc_blk_num]
	add	w10, w10, #1                    // =1
	strh	w10, [x16, :lo12:g_gc_blk_num]
.LBB125_38:                             //   in Loop: Header=BB125_18 Depth=3
	ldr	x10, [x24, :lo12:p_gc_page_info]
	ldrh	w11, [x26, :lo12:g_gc_page_offset]
	madd	x10, x11, x27, x10
	str	w23, [x10, #4]
	ldr	x10, [x24, :lo12:p_gc_page_info]
	ldrh	w11, [x26, :lo12:g_gc_page_offset]
	madd	x10, x11, x27, x10
	str	w9, [x10, #8]
	b	.LBB125_17
.LBB125_39:                             //   in Loop: Header=BB125_7 Depth=2
	ldr	x8, [sp, #24]                   // 8-byte Folded Reload
	ldr	w2, [sp, #44]
	adrp	x0, .L.str.111
	add	x0, x0, :lo12:.L.str.111
	ldrh	w1, [x8]
	bl	sftl_printk
.LBB125_40:                             //   in Loop: Header=BB125_7 Depth=2
	ldr	x22, [sp, #24]                  // 8-byte Folded Reload
	adrp	x8, p_valid_page_count_table
	ldr	x8, [x8, :lo12:p_valid_page_count_table]
	ldrh	w9, [x22]
	strh	wzr, [x8, x9, lsl #1]
	ldrh	w0, [x22]
	bl	INSERT_FREE_LIST
	strh	w21, [x22]
	adrp	x8, g_gc_superblock
	adrp	x19, c_ftl_nand_page_pre_super_blk
	strh	w21, [x8, :lo12:g_gc_superblock]
	ldrh	w8, [x19, :lo12:c_ftl_nand_page_pre_super_blk]
	adrp	x9, p_gc_blk_tbl
	ldr	x0, [x9, :lo12:p_gc_blk_tbl]
	adrp	x9, g_gc_blk_num
	lsl	x2, x8, #1
	mov	w1, #255
	strh	wzr, [x9, :lo12:g_gc_blk_num]
	strh	wzr, [x26, :lo12:g_gc_page_offset]
	bl	memset
	ldrh	w8, [x19, :lo12:c_ftl_nand_page_pre_super_blk]
	ldr	x0, [x24, :lo12:p_gc_page_info]
	mov	w1, #255
	add	x8, x8, x8, lsl #1
	lsl	x2, x8, #2
	bl	memset
	bl	FtlGcBufInit
	ldrsh	w8, [x22]
	mov	w9, wzr
	mov	w19, wzr
	adrp	x16, c_ftl_nand_planes_num
	cmn	w8, #1                          // =1
	adrp	x17, p_gc_data_buf
	adrp	x18, c_ftl_nand_byte_pre_page
	adrp	x0, p_gc_spare_buf
	adrp	x1, c_ftl_nand_byte_pre_oob
	strb	wzr, [x22, #8]
	b.ne	.LBB125_7
	b	.LBB125_44
.LBB125_41:                             //   in Loop: Header=BB125_6 Depth=1
	ldp	w8, w10, [sp, #12]              // 8-byte Folded Reload
	ldr	x22, [sp, #24]                  // 8-byte Folded Reload
	add	w10, w10, #1                    // =1
	cmp	w10, w8
	ldr	w8, [sp, #20]                   // 4-byte Folded Reload
	add	w19, w8, #1                     // =1
	b.lo	.LBB125_5
// %bb.42:                              //   in Loop: Header=BB125_6 Depth=1
	adrp	x8, ftl_gc_temp_block_bops_scan_page_addr
	ldrh	w8, [x8, :lo12:ftl_gc_temp_block_bops_scan_page_addr]
	cmp	w8, w21
	b.eq	.LBB125_5
// %bb.43:                              //   in Loop: Header=BB125_6 Depth=1
	adrp	x9, c_ftl_nand_page_pre_blk
	ldrh	w9, [x9, :lo12:c_ftl_nand_page_pre_blk]
	add	w8, w8, w10
	cmp	w9, w19, uxth
	adrp	x9, ftl_gc_temp_block_bops_scan_page_addr
	strh	w8, [x9, :lo12:ftl_gc_temp_block_bops_scan_page_addr]
	b.ls	.LBB125_5
	b	.LBB125_53
.LBB125_44:
	mov	w8, #65535
	adrp	x9, ftl_gc_temp_block_bops_scan_page_addr
	strh	w8, [x9, :lo12:ftl_gc_temp_block_bops_scan_page_addr]
	strh	w19, [x22, #2]
	strb	wzr, [x22, #6]
	strh	wzr, [x22, #4]
	ldrh	w8, [x16, :lo12:c_ftl_nand_planes_num]
	cbz	x8, .LBB125_53
// %bb.45:
	mov	w9, wzr
	add	x10, x22, #16                   // =16
	mov	x11, x8
	b	.LBB125_47
.LBB125_46:                             //   in Loop: Header=BB125_47 Depth=1
	subs	x11, x11, #1                    // =1
	add	x10, x10, #2                    // =2
	b.eq	.LBB125_49
.LBB125_47:                             // =>This Inner Loop Header: Depth=1
	ldrsh	w12, [x10]
	cmn	w12, #1                         // =1
	b.eq	.LBB125_46
// %bb.48:                              //   in Loop: Header=BB125_47 Depth=1
	add	w9, w9, #1                      // =1
	strh	w9, [x22, #4]
	b	.LBB125_46
.LBB125_49:
	adrp	x10, c_ftl_nand_page_pre_blk
	ldrh	w10, [x10, :lo12:c_ftl_nand_page_pre_blk]
	mvn	w11, w19
	add	w10, w10, w11
	add	x11, x22, #16                   // =16
	b	.LBB125_51
.LBB125_50:                             //   in Loop: Header=BB125_51 Depth=1
	subs	x8, x8, #1                      // =1
	add	x11, x11, #2                    // =2
	b.eq	.LBB125_53
.LBB125_51:                             // =>This Inner Loop Header: Depth=1
	ldrsh	w12, [x11]
	cmn	w12, #1                         // =1
	b.eq	.LBB125_50
// %bb.52:                              //   in Loop: Header=BB125_51 Depth=1
	add	w9, w10, w9
	strh	w9, [x22, #4]
	b	.LBB125_50
.LBB125_53:
	adrp	x9, __stack_chk_guard
	ldur	x8, [x29, #-8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB125_55
// %bb.54:
	ldp	x20, x19, [sp, #160]            // 16-byte Folded Reload
	ldp	x22, x21, [sp, #144]            // 16-byte Folded Reload
	ldp	x24, x23, [sp, #128]            // 16-byte Folded Reload
	ldp	x26, x25, [sp, #112]            // 16-byte Folded Reload
	ldp	x28, x27, [sp, #96]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #80]             // 16-byte Folded Reload
	mov	w0, #-1
	add	sp, sp, #176                    // =176
	hint	#29
	ret
.LBB125_55:
	bl	__stack_chk_fail
.Lfunc_end125:
	.size	FtlGcScanTempBlk, .Lfunc_end125-FtlGcScanTempBlk
                                        // -- End function
	.globl	FtlGcPageVarInit                // -- Begin function FtlGcPageVarInit
	.p2align	2
	.type	FtlGcPageVarInit,@function
FtlGcPageVarInit:                       // @FtlGcPageVarInit
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	str	x19, [sp, #16]                  // 8-byte Folded Spill
	adrp	x8, g_gc_blk_num
	adrp	x19, c_ftl_nand_page_pre_super_blk
	adrp	x9, p_gc_blk_tbl
	strh	wzr, [x8, :lo12:g_gc_blk_num]
	ldrh	w8, [x19, :lo12:c_ftl_nand_page_pre_super_blk]
	ldr	x0, [x9, :lo12:p_gc_blk_tbl]
	adrp	x9, g_gc_page_offset
	mov	w1, #255
	lsl	x2, x8, #1
	mov	x29, sp
	strh	wzr, [x9, :lo12:g_gc_page_offset]
	bl	memset
	ldrh	w8, [x19, :lo12:c_ftl_nand_page_pre_super_blk]
	adrp	x9, p_gc_page_info
	ldr	x0, [x9, :lo12:p_gc_page_info]
	mov	w1, #255
	add	x8, x8, x8, lsl #1
	lsl	x2, x8, #2
	bl	memset
	bl	FtlGcBufInit
	ldr	x19, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end126:
	.size	FtlGcPageVarInit, .Lfunc_end126-FtlGcPageVarInit
                                        // -- End function
	.globl	Ftl_get_new_temp_ppa            // -- Begin function Ftl_get_new_temp_ppa
	.p2align	2
	.type	Ftl_get_new_temp_ppa,@function
Ftl_get_new_temp_ppa:                   // @Ftl_get_new_temp_ppa
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-64]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #48]             // 16-byte Folded Spill
	adrp	x19, g_gc_temp_superblock
	add	x19, x19, :lo12:g_gc_temp_superblock
	ldrsh	w8, [x19]
	str	x23, [sp, #16]                  // 8-byte Folded Spill
	stp	x22, x21, [sp, #32]             // 16-byte Folded Spill
	mov	x29, sp
	cmn	w8, #1                          // =1
	b.eq	.LBB127_2
// %bb.1:
	ldrh	w8, [x19, #4]
	cbnz	w8, .LBB127_8
.LBB127_2:
	mov	w0, wzr
	bl	FtlGcFreeTempBlock
	mov	x0, x19
	strb	wzr, [x19, #8]
	bl	allocate_data_superblock
	adrp	x8, g_gc_blk_num
	adrp	x9, g_gc_page_offset
	strh	wzr, [x8, :lo12:g_gc_blk_num]
	strh	wzr, [x9, :lo12:g_gc_page_offset]
	bl	FtlWriteDump_data
	adrp	x20, c_ftl_nand_l2pmap_ram_region_num
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	cbz	w8, .LBB127_7
// %bb.3:
	adrp	x19, gL2pMapInfo
	mov	x21, xzr
	mov	x22, xzr
	adrp	x23, p_l2p_ram_map
	add	x19, x19, :lo12:gL2pMapInfo
	b	.LBB127_5
.LBB127_4:                              //   in Loop: Header=BB127_5 Depth=1
	add	x22, x22, #1                    // =1
	cmp	x22, w8, uxth
	add	x21, x21, #16                   // =16
	b.hs	.LBB127_7
.LBB127_5:                              // =>This Inner Loop Header: Depth=1
	ldr	x9, [x23, :lo12:p_l2p_ram_map]
	add	x9, x9, x21
	ldr	w10, [x9, #4]
	tbz	w10, #31, .LBB127_4
// %bb.6:                               //   in Loop: Header=BB127_5 Depth=1
	ldrh	w1, [x9]
	ldr	x2, [x9, #8]
	mov	x0, x19
	bl	FtlMapWritePage
	ldr	x8, [x23, :lo12:p_l2p_ram_map]
	add	x8, x8, x21
	ldr	w9, [x8, #4]
	and	w9, w9, #0x7fffffff
	str	w9, [x8, #4]
	ldrh	w8, [x20, :lo12:c_ftl_nand_l2pmap_ram_region_num]
	b	.LBB127_4
.LBB127_7:
	mov	w0, wzr
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.LBB127_8:
	adrp	x0, g_gc_temp_superblock
	add	x0, x0, :lo12:g_gc_temp_superblock
	bl	get_new_active_ppa
	ldp	x20, x19, [sp, #48]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #32]             // 16-byte Folded Reload
	ldr	x23, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #64             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end127:
	.size	Ftl_get_new_temp_ppa, .Lfunc_end127-Ftl_get_new_temp_ppa
                                        // -- End function
	.globl	Ftl_gc_temp_data_write_back     // -- Begin function Ftl_gc_temp_data_write_back
	.p2align	2
	.type	Ftl_gc_temp_data_write_back,@function
Ftl_gc_temp_data_write_back:            // @Ftl_gc_temp_data_write_back
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	stp	x20, x19, [sp, #16]             // 16-byte Folded Spill
	adrp	x19, req_gc_dst
	adrp	x20, g_gc_num_req
	ldr	x0, [x19, :lo12:req_gc_dst]
	ldr	w1, [x20, :lo12:g_gc_num_req]
	mov	w2, wzr
	mov	x29, sp
	bl	FlashProgPages
	ldr	w8, [x20, :lo12:g_gc_num_req]
	cbz	w8, .LBB128_25
// %bb.1:
	mov	w8, wzr
	adrp	x9, c_ftl_nand_blks_per_die
	adrp	x10, c_ftl_nand_planes_per_die
	adrp	x11, g_gc_blk_num
	adrp	x12, p_gc_blk_tbl
	adrp	x13, p_gc_page_info
	adrp	x14, g_gc_page_offset
	mov	w15, #12
	b	.LBB128_4
.LBB128_2:                              //   in Loop: Header=BB128_4 Depth=1
	strh	w2, [x1, x0, lsl #1]
	ldrh	w0, [x11, :lo12:g_gc_blk_num]
	add	w0, w0, #1                      // =1
	strh	w0, [x11, :lo12:g_gc_blk_num]
.LBB128_3:                              //   in Loop: Header=BB128_4 Depth=1
	ldr	x0, [x13, :lo12:p_gc_page_info]
	ldrh	w1, [x14, :lo12:g_gc_page_offset]
	add	w8, w8, #1                      // =1
	madd	x0, x1, x15, x0
	str	w17, [x0, #4]
	ldr	x17, [x13, :lo12:p_gc_page_info]
	ldrh	w0, [x14, :lo12:g_gc_page_offset]
	madd	x17, x0, x15, x17
	str	w18, [x17, #8]
	ldrh	w17, [x14, :lo12:g_gc_page_offset]
	ldr	x18, [x13, :lo12:p_gc_page_info]
	mul	x17, x17, x15
	str	w16, [x18, x17]
	ldrh	w17, [x14, :lo12:g_gc_page_offset]
	ldr	w16, [x20, :lo12:g_gc_num_req]
	add	w17, w17, #1                    // =1
	cmp	w16, w8, uxth
	strh	w17, [x14, :lo12:g_gc_page_offset]
	b.ls	.LBB128_17
.LBB128_4:                              // =>This Loop Header: Depth=1
                                        //     Child Loop BB128_7 Depth 2
                                        //     Child Loop BB128_11 Depth 2
	ldr	x16, [x19, :lo12:req_gc_dst]
	and	x17, x8, #0xffff
	add	x17, x16, x17, lsl #5
	ldr	x18, [x17, #16]
	ldp	w0, w17, [x17]
	ldr	w16, [x18, #12]
	cmn	w0, #1                          // =1
	b.eq	.LBB128_9
// %bb.5:                               //   in Loop: Header=BB128_4 Depth=1
	ldrh	w3, [x9, :lo12:c_ftl_nand_blks_per_die]
	ldrh	w4, [x10, :lo12:c_ftl_nand_planes_per_die]
	ldr	w18, [x18, #8]
	ubfx	w2, w16, #10, #16
	ldrh	w0, [x11, :lo12:g_gc_blk_num]
	ldr	x1, [x12, :lo12:p_gc_blk_tbl]
	udiv	w5, w2, w3
	msub	w2, w5, w3, w2
	udiv	w2, w2, w4
	cbz	x0, .LBB128_2
// %bb.6:                               //   in Loop: Header=BB128_4 Depth=1
	mov	x3, xzr
.LBB128_7:                              //   Parent Loop BB128_4 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldrh	w4, [x1, x3, lsl #1]
	cmp	w4, w2, uxth
	b.eq	.LBB128_13
// %bb.8:                               //   in Loop: Header=BB128_7 Depth=2
	add	x3, x3, #1                      // =1
	cmp	x0, x3
	b.ne	.LBB128_7
	b	.LBB128_2
.LBB128_9:                              //   in Loop: Header=BB128_4 Depth=1
	ldrh	w2, [x9, :lo12:c_ftl_nand_blks_per_die]
	ldrh	w3, [x10, :lo12:c_ftl_nand_planes_per_die]
	ubfx	w1, w16, #10, #16
	ldrh	w18, [x11, :lo12:g_gc_blk_num]
	ldr	x0, [x12, :lo12:p_gc_blk_tbl]
	udiv	w4, w1, w2
	msub	w1, w4, w2, w1
	udiv	w1, w1, w3
	cbz	x18, .LBB128_15
// %bb.10:                              //   in Loop: Header=BB128_4 Depth=1
	mov	x2, xzr
.LBB128_11:                             //   Parent Loop BB128_4 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	ldrh	w3, [x0, x2, lsl #1]
	cmp	w3, w1, uxth
	b.eq	.LBB128_14
// %bb.12:                              //   in Loop: Header=BB128_11 Depth=2
	add	x2, x2, #1                      // =1
	cmp	x18, x2
	b.ne	.LBB128_11
	b	.LBB128_15
.LBB128_13:                             //   in Loop: Header=BB128_4 Depth=1
	cmp	w0, w3, uxth
	b.ne	.LBB128_3
	b	.LBB128_2
.LBB128_14:                             //   in Loop: Header=BB128_4 Depth=1
	cmp	w18, w2, uxth
	b.ne	.LBB128_16
.LBB128_15:                             //   in Loop: Header=BB128_4 Depth=1
	strh	w1, [x0, x18, lsl #1]
	ldrh	w18, [x11, :lo12:g_gc_blk_num]
	add	w18, w18, #1                    // =1
	strh	w18, [x11, :lo12:g_gc_blk_num]
.LBB128_16:                             //   in Loop: Header=BB128_4 Depth=1
	mov	w18, #-1
	b	.LBB128_3
.LBB128_17:
	cbz	w16, .LBB128_25
// %bb.18:
	ldr	x9, [x19, :lo12:req_gc_dst]
	mov	w8, wzr
	adrp	x10, c_gc_page_buf_num
	adrp	x11, gp_gc_page_buf_info
	mov	w12, #24
	b	.LBB128_21
.LBB128_19:                             //   in Loop: Header=BB128_21 Depth=1
                                        // kill: def $w17 killed $w17 killed $x17 def $x17
	and	x14, x17, #0xffff
	madd	x13, x14, x12, x13
	str	wzr, [x13, #16]
.LBB128_20:                             //   in Loop: Header=BB128_21 Depth=1
	add	w8, w8, #1                      // =1
	cmp	w16, w8, uxth
	b.ls	.LBB128_25
.LBB128_21:                             // =>This Loop Header: Depth=1
                                        //     Child Loop BB128_23 Depth 2
	ldr	w14, [x10, :lo12:c_gc_page_buf_num]
	cbz	w14, .LBB128_20
// %bb.22:                              //   in Loop: Header=BB128_21 Depth=1
	and	x15, x8, #0xffff
	add	x15, x9, x15, lsl #5
	ldr	x13, [x11, :lo12:gp_gc_page_buf_info]
	ldr	x15, [x15, #8]
	mov	w17, wzr
.LBB128_23:                             //   Parent Loop BB128_21 Depth=1
                                        // =>  This Inner Loop Header: Depth=2
	and	x18, x17, #0xffff
	mul	x18, x18, x12
	ldr	x18, [x13, x18]
	cmp	x18, x15
	b.eq	.LBB128_19
// %bb.24:                              //   in Loop: Header=BB128_23 Depth=2
	add	w17, w17, #1                    // =1
	cmp	w14, w17, uxth
	b.hi	.LBB128_23
	b	.LBB128_20
.LBB128_25:
	adrp	x8, g_gc_temp_superblock+4
	ldrh	w8, [x8, :lo12:g_gc_temp_superblock+4]
	mov	w19, wzr
	str	wzr, [x20, :lo12:g_gc_num_req]
	cbnz	w8, .LBB128_27
// %bb.26:
	mov	w0, #1
	mov	w19, #1
	bl	FtlGcFreeTempBlock
.LBB128_27:
	mov	w0, w19
	ldp	x20, x19, [sp, #16]             // 16-byte Folded Reload
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end128:
	.size	Ftl_gc_temp_data_write_back, .Lfunc_end128-Ftl_gc_temp_data_write_back
                                        // -- End function
	.globl	FtlGcMarkBadPhyBlk              // -- Begin function FtlGcMarkBadPhyBlk
	.p2align	2
	.type	FtlGcMarkBadPhyBlk,@function
FtlGcMarkBadPhyBlk:                     // @FtlGcMarkBadPhyBlk
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-48]!           // 16-byte Folded Spill
	adrp	x8, c_ftl_nand_blks_per_die
	ldrh	w8, [x8, :lo12:c_ftl_nand_blks_per_die]
	str	x21, [sp, #16]                  // 8-byte Folded Spill
	adrp	x9, c_ftl_nand_planes_per_die
	adrp	x21, g_gc_bad_block_temp_num
	ldrh	w9, [x9, :lo12:c_ftl_nand_planes_per_die]
	ldrh	w1, [x21, :lo12:g_gc_bad_block_temp_num]
	and	w2, w0, #0xffff
	stp	x20, x19, [sp, #32]             // 16-byte Folded Spill
	mov	w19, w0
	udiv	w10, w2, w8
	adrp	x0, .L.str.113
	msub	w8, w10, w8, w2
	add	x0, x0, :lo12:.L.str.113
	mov	x29, sp
	udiv	w20, w8, w9
	bl	sftl_printk
	adrp	x0, .L.str.112
	add	x0, x0, :lo12:.L.str.112
	mov	w1, w20
	bl	sftl_printk
	adrp	x8, g_gc_next_blk
	ldrh	w10, [x8, :lo12:g_gc_next_blk]
	cmp	w10, w20
	b.eq	.LBB129_5
// %bb.1:
	adrp	x9, g_gc_next_blk_1
	ldrh	w11, [x9, :lo12:g_gc_next_blk_1]
	cmp	w11, w20, uxth
	b.eq	.LBB129_5
// %bb.2:
	mov	w12, #65535
	cmp	w10, w12
	b.eq	.LBB129_12
// %bb.3:
	cmp	w11, w12
	b.ne	.LBB129_5
// %bb.4:
	strh	w20, [x9, :lo12:g_gc_next_blk_1]
.LBB129_5:
	ldrh	w8, [x21, :lo12:g_gc_bad_block_temp_num]
	cbz	x8, .LBB129_10
.LBB129_6:
	adrp	x9, g_gc_bad_block_temp_tbl
	add	x9, x9, :lo12:g_gc_bad_block_temp_tbl
	mov	x10, x8
.LBB129_7:                              // =>This Inner Loop Header: Depth=1
	ldrh	w11, [x9]
	cmp	w11, w19, uxth
	b.eq	.LBB129_11
// %bb.8:                               //   in Loop: Header=BB129_7 Depth=1
	subs	x10, x10, #1                    // =1
	add	x9, x9, #2                      // =2
	b.ne	.LBB129_7
// %bb.9:
	cmp	w8, #15                         // =15
	b.hi	.LBB129_11
.LBB129_10:
	adrp	x9, g_gc_bad_block_temp_tbl
	add	x9, x9, :lo12:g_gc_bad_block_temp_tbl
	add	w10, w8, #1                     // =1
	strh	w10, [x21, :lo12:g_gc_bad_block_temp_num]
	strh	w19, [x9, w8, uxtw #1]
.LBB129_11:
	ldp	x20, x19, [sp, #32]             // 16-byte Folded Reload
	ldr	x21, [sp, #16]                  // 8-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #48             // 16-byte Folded Reload
	hint	#29
	ret
.LBB129_12:
	strh	w20, [x8, :lo12:g_gc_next_blk]
	ldrh	w8, [x21, :lo12:g_gc_bad_block_temp_num]
	cbnz	x8, .LBB129_6
	b	.LBB129_10
.Lfunc_end129:
	.size	FtlGcMarkBadPhyBlk, .Lfunc_end129-FtlGcMarkBadPhyBlk
                                        // -- End function
	.globl	FtlGcReFreshBadBlk              // -- Begin function FtlGcReFreshBadBlk
	.p2align	2
	.type	FtlGcReFreshBadBlk,@function
FtlGcReFreshBadBlk:                     // @FtlGcReFreshBadBlk
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-48]!           // 16-byte Folded Spill
	adrp	x8, g_gc_bad_block_temp_num
	ldrh	w9, [x8, :lo12:g_gc_bad_block_temp_num]
	str	x21, [sp, #16]                  // 8-byte Folded Spill
	stp	x20, x19, [sp, #32]             // 16-byte Folded Spill
	mov	x29, sp
	cbz	w9, .LBB130_11
// %bb.1:
	adrp	x21, g_gc_next_blk
	ldrh	w8, [x21, :lo12:g_gc_next_blk]
	mov	w10, #65535
	cmp	w8, w10
	b.ne	.LBB130_11
// %bb.2:
	adrp	x20, g_gc_bad_block_gc_index
	ldrh	w8, [x20, :lo12:g_gc_bad_block_gc_index]
	cmp	w8, w9
	b.lo	.LBB130_4
// %bb.3:
	mov	w8, wzr
	strh	wzr, [x20, :lo12:g_gc_bad_block_gc_index]
.LBB130_4:
	adrp	x9, g_gc_bad_block_temp_tbl
	add	x9, x9, :lo12:g_gc_bad_block_temp_tbl
	adrp	x10, c_ftl_nand_blks_per_die
	ldrh	w8, [x9, w8, uxtw #1]
	ldrh	w9, [x10, :lo12:c_ftl_nand_blks_per_die]
	adrp	x10, c_ftl_nand_planes_per_die
	ldrh	w10, [x10, :lo12:c_ftl_nand_planes_per_die]
	adrp	x0, .L.str.112
	udiv	w11, w8, w9
	msub	w8, w11, w9, w8
	udiv	w19, w8, w10
	add	x0, x0, :lo12:.L.str.112
	mov	w1, w19
	bl	sftl_printk
	ldrh	w9, [x21, :lo12:g_gc_next_blk]
	cmp	w9, w19
	b.eq	.LBB130_10
// %bb.5:
	adrp	x8, g_gc_next_blk_1
	ldrh	w10, [x8, :lo12:g_gc_next_blk_1]
	cmp	w10, w19, uxth
	b.eq	.LBB130_10
// %bb.6:
	mov	w11, #65535
	cmp	w9, w11
	b.eq	.LBB130_9
// %bb.7:
	cmp	w10, w11
	b.ne	.LBB130_10
// %bb.8:
	strh	w19, [x8, :lo12:g_gc_next_blk_1]
	b	.LBB130_10
.LBB130_9:
	strh	w19, [x21, :lo12:g_gc_next_blk]
.LBB130_10:
	ldrh	w8, [x20, :lo12:g_gc_bad_block_gc_index]
	add	w8, w8, #1                      // =1
	strh	w8, [x20, :lo12:g_gc_bad_block_gc_index]
.LBB130_11:
	ldp	x20, x19, [sp, #32]             // 16-byte Folded Reload
	ldr	x21, [sp, #16]                  // 8-byte Folded Reload
	mov	w0, wzr
	ldp	x29, x30, [sp], #48             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end130:
	.size	FtlGcReFreshBadBlk, .Lfunc_end130-FtlGcReFreshBadBlk
                                        // -- End function
	.globl	FtlReadRefresh                  // -- Begin function FtlReadRefresh
	.p2align	2
	.type	FtlReadRefresh,@function
FtlReadRefresh:                         // @FtlReadRefresh
// %bb.0:
	hint	#25
	sub	sp, sp, #160                    // =160
	adrp	x8, __stack_chk_guard
	ldr	x8, [x8, :lo12:__stack_chk_guard]
	stp	x29, x30, [sp, #112]            // 16-byte Folded Spill
	add	x29, sp, #112                   // =112
	stp	x22, x21, [sp, #128]            // 16-byte Folded Spill
	stp	x20, x19, [sp, #144]            // 16-byte Folded Spill
	stur	x8, [x29, #-8]
	adrp	x8, g_sys_ext_data+80
	ldr	w8, [x8, :lo12:g_sys_ext_data+80]
	cbz	w8, .LBB131_6
// %bb.1:
	adrp	x19, g_sys_ext_data+84
	adrp	x20, g_MaxLpn
	ldr	w8, [x19, :lo12:g_sys_ext_data+84]
	ldr	w9, [x20, :lo12:g_MaxLpn]
	cmp	w8, w9
	b.hs	.LBB131_11
// %bb.2:
	mov	w21, #2047
.LBB131_3:                              // =>This Inner Loop Header: Depth=1
	add	x1, sp, #36                     // =36
	mov	w0, w8
	mov	w2, wzr
	bl	log2phys
	ldr	w8, [x19, :lo12:g_sys_ext_data+84]
	ldr	w22, [sp, #36]
	add	w8, w8, #1                      // =1
	cmn	w22, #1                         // =1
	str	w8, [x19, :lo12:g_sys_ext_data+84]
	b.ne	.LBB131_12
// %bb.4:                               //   in Loop: Header=BB131_3 Depth=1
	subs	w21, w21, #1                    // =1
	mov	w0, #-1
	b.lo	.LBB131_20
// %bb.5:                               //   in Loop: Header=BB131_3 Depth=1
	ldr	w9, [x20, :lo12:g_MaxLpn]
	cmp	w8, w9
	b.lo	.LBB131_3
	b	.LBB131_20
.LBB131_6:
	adrp	x8, g_max_erase_count
	adrp	x11, g_totle_read_page_count
	ldr	w12, [x8, :lo12:g_max_erase_count]
	ldr	w8, [x11, :lo12:g_totle_read_page_count]
	adrp	x11, g_sys_ext_data+76
	ldr	w11, [x11, :lo12:g_sys_ext_data+76]
	mov	w9, #10000
	mov	w10, #63
	mov	w13, #31
	cmp	w12, w9
	add	w9, w8, #256, lsl #12           // =1048576
	csel	w10, w13, w10, hi
	cmp	w11, w9
	adrp	x9, g_sys_save_data+28
	b.hi	.LBB131_10
// %bb.7:
	adrp	x13, g_MaxLpn
	ldr	w13, [x13, :lo12:g_MaxLpn]
	mov	w14, #1000
	lsr	w12, w12, #10
	add	w12, w12, #1                    // =1
	mul	w13, w13, w14
	udiv	w12, w13, w12
	add	w11, w12, w11
	cmp	w11, w8
	b.lo	.LBB131_10
// %bb.8:
	adrp	x12, g_sys_ext_data+100
	ldrh	w11, [x9, :lo12:g_sys_save_data+28]
	ldr	w12, [x12, :lo12:g_sys_ext_data+100]
	mov	w0, wzr
	cmp	w12, w11
	b.eq	.LBB131_20
// %bb.9:
	and	w10, w10, w11
	cbnz	w10, .LBB131_20
.LBB131_10:
	ldrh	w9, [x9, :lo12:g_sys_save_data+28]
	adrp	x10, g_sys_ext_data+76
	mov	w0, wzr
	add	x10, x10, :lo12:g_sys_ext_data+76
	mov	w11, #1
	stur	x11, [x10, #4]
	str	w9, [x10, #24]
	str	w8, [x10]
	b	.LBB131_20
.LBB131_11:
	adrp	x8, g_totle_read_page_count
	ldr	w8, [x8, :lo12:g_totle_read_page_count]
	adrp	x9, g_sys_ext_data+76
	add	x9, x9, :lo12:g_sys_ext_data+76
	mov	w0, wzr
	stur	xzr, [x9, #4]
	str	w8, [x9]
	b	.LBB131_20
.LBB131_12:
	adrp	x9, sftl_temp_buf
	ldr	x9, [x9, :lo12:sftl_temp_buf]
	str	w8, [sp, #24]
	add	x8, sp, #40                     // =40
	mov	x0, sp
	mov	w1, #1
	stp	x9, x8, [sp, #8]
	stp	wzr, w22, [sp]
	bl	FlashReadPages
	ldr	w8, [sp]
	cmp	w8, #256                        // =256
	b.ne	.LBB131_19
// %bb.13:
	adrp	x8, c_ftl_nand_blks_per_die
	ldrh	w8, [x8, :lo12:c_ftl_nand_blks_per_die]
	adrp	x9, c_ftl_nand_planes_per_die
	ldrh	w9, [x9, :lo12:c_ftl_nand_planes_per_die]
	ubfx	w10, w22, #10, #16
	udiv	w11, w10, w8
	msub	w8, w11, w8, w10
	udiv	w19, w8, w9
	adrp	x0, .L.str.112
	add	x0, x0, :lo12:.L.str.112
	mov	w1, w19
	bl	sftl_printk
	adrp	x8, g_gc_next_blk
	ldrh	w10, [x8, :lo12:g_gc_next_blk]
	cmp	w10, w19
	b.eq	.LBB131_19
// %bb.14:
	adrp	x9, g_gc_next_blk_1
	ldrh	w11, [x9, :lo12:g_gc_next_blk_1]
	cmp	w11, w19, uxth
	b.eq	.LBB131_19
// %bb.15:
	mov	w12, #65535
	cmp	w10, w12
	b.eq	.LBB131_18
// %bb.16:
	cmp	w11, w12
	b.ne	.LBB131_19
// %bb.17:
	strh	w19, [x9, :lo12:g_gc_next_blk_1]
	b	.LBB131_19
.LBB131_18:
	strh	w19, [x8, :lo12:g_gc_next_blk]
.LBB131_19:
	mov	w0, #-1
.LBB131_20:
	adrp	x9, __stack_chk_guard
	ldur	x8, [x29, #-8]
	ldr	x9, [x9, :lo12:__stack_chk_guard]
	cmp	x9, x8
	b.ne	.LBB131_22
// %bb.21:
	ldp	x20, x19, [sp, #144]            // 16-byte Folded Reload
	ldp	x22, x21, [sp, #128]            // 16-byte Folded Reload
	ldp	x29, x30, [sp, #112]            // 16-byte Folded Reload
	add	sp, sp, #160                    // =160
	hint	#29
	ret
.LBB131_22:
	bl	__stack_chk_fail
.Lfunc_end131:
	.size	FtlReadRefresh, .Lfunc_end131-FtlReadRefresh
                                        // -- End function
	.globl	sftl_gc                         // -- Begin function sftl_gc
	.p2align	2
	.type	sftl_gc,@function
sftl_gc:                                // @sftl_gc
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	mov	w0, #1
	mov	x29, sp
	bl	rk_ftl_garbage_collect
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end132:
	.size	sftl_gc, .Lfunc_end132-sftl_gc
                                        // -- End function
	.globl	ftl_free                        // -- Begin function ftl_free
	.p2align	2
	.type	ftl_free,@function
ftl_free:                               // @ftl_free
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	mov	x29, sp
	bl	kfree
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end133:
	.size	ftl_free, .Lfunc_end133-ftl_free
                                        // -- End function
	.globl	ftl_memcmp                      // -- Begin function ftl_memcmp
	.p2align	2
	.type	ftl_memcmp,@function
ftl_memcmp:                             // @ftl_memcmp
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	mov	w2, w2
	mov	x29, sp
	bl	memcmp
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end134:
	.size	ftl_memcmp, .Lfunc_end134-ftl_memcmp
                                        // -- End function
	.globl	rk_sftl_vendor_dev_ops_register // -- Begin function rk_sftl_vendor_dev_ops_register
	.p2align	2
	.type	rk_sftl_vendor_dev_ops_register,@function
rk_sftl_vendor_dev_ops_register:        // @rk_sftl_vendor_dev_ops_register
// %bb.0:
	hint	#25
	adrp	x9, _flash_read
	ldr	x10, [x9, :lo12:_flash_read]
	cbz	x10, .LBB135_2
// %bb.1:
	mov	w0, #-1
	hint	#29
	ret
.LBB135_2:
	mov	x8, x0
	str	x8, [x9, :lo12:_flash_read]
	adrp	x8, _flash_write
	mov	w0, wzr
	str	x1, [x8, :lo12:_flash_write]
	hint	#29
	ret
.Lfunc_end135:
	.size	rk_sftl_vendor_dev_ops_register, .Lfunc_end135-rk_sftl_vendor_dev_ops_register
                                        // -- End function
	.globl	rk_sftl_vendor_storage_init     // -- Begin function rk_sftl_vendor_storage_init
	.p2align	2
	.type	rk_sftl_vendor_storage_init,@function
rk_sftl_vendor_storage_init:            // @rk_sftl_vendor_storage_init
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-64]!           // 16-byte Folded Spill
	mov	w0, #65536
	mov	w1, #3265
	mov	w2, #4
	str	x23, [sp, #16]                  // 8-byte Folded Spill
	stp	x22, x21, [sp, #32]             // 16-byte Folded Spill
	stp	x20, x19, [sp, #48]             // 16-byte Folded Spill
	mov	x29, sp
	bl	kmalloc_order_trace
	adrp	x20, g_vendor
	str	x0, [x20, :lo12:g_vendor]
	cbz	x0, .LBB136_4
// %bb.1:
	adrp	x22, _flash_read
	ldr	x8, [x22, :lo12:_flash_read]
	mov	x2, x0
	mov	w1, #128
	mov	w0, wzr
	blr	x8
	cbnz	w0, .LBB136_7
// %bb.2:
	ldr	x2, [x20, :lo12:g_vendor]
	mov	w21, #22084
	movk	w21, #21067, lsl #16
	ldr	w8, [x2]
	cmp	w8, w21
	b.ne	.LBB136_5
// %bb.3:
	mov	w8, #65532
	ldr	w9, [x2, #4]
	ldr	w8, [x2, x8]
	cmp	w9, #0                          // =0
	ccmp	w8, w9, #0, ne
	csel	w23, w9, wzr, eq
	b	.LBB136_6
.LBB136_4:
	mov	w0, #-12
	b	.LBB136_8
.LBB136_5:
	mov	w23, wzr
.LBB136_6:
	ldr	x8, [x22, :lo12:_flash_read]
	mov	w0, #128
	mov	w1, #128
	blr	x8
	cbz	w0, .LBB136_9
.LBB136_7:
	ldr	x0, [x20, :lo12:g_vendor]
	bl	kfree
	mov	w0, #-1
	str	xzr, [x20, :lo12:g_vendor]
.LBB136_8:
	ldp	x20, x19, [sp, #48]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #32]             // 16-byte Folded Reload
	ldr	x23, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #64             // 16-byte Folded Reload
	hint	#29
	ret
.LBB136_9:
	ldr	x19, [x20, :lo12:g_vendor]
	ldr	w8, [x19]
	cmp	w8, w21
	b.ne	.LBB136_12
// %bb.10:
	mov	w8, #65532
	ldr	w9, [x19, #4]
	ldr	w8, [x19, x8]
	mov	w10, #128
	cmp	w8, w9
	cset	w8, eq
	cmp	w23, w9
	cset	w11, lo
	tst	w8, w11
	csel	w23, w9, w23, ne
	csel	w0, w10, wzr, ne
	cbz	w23, .LBB136_13
.LBB136_11:
	ldr	x8, [x22, :lo12:_flash_read]
	mov	w1, #128
	mov	x2, x19
	blr	x8
	cbnz	w0, .LBB136_7
	b	.LBB136_8
.LBB136_12:
	mov	w0, wzr
	cbnz	w23, .LBB136_11
.LBB136_13:
	add	x0, x19, #8                     // =8
	mov	w2, #65524
	mov	w1, wzr
	bl	memset
	mov	w8, #1
	mov	w9, #65532
	mov	w10, #-67633152
	mov	w0, wzr
	stp	w21, w8, [x19]
	str	w8, [x19, x9]
	str	w10, [x19, #12]
	b	.LBB136_8
.Lfunc_end136:
	.size	rk_sftl_vendor_storage_init, .Lfunc_end136-rk_sftl_vendor_storage_init
                                        // -- End function
	.globl	rk_sftl_vendor_read             // -- Begin function rk_sftl_vendor_read
	.p2align	2
	.type	rk_sftl_vendor_read,@function
rk_sftl_vendor_read:                    // @rk_sftl_vendor_read
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-32]!           // 16-byte Folded Spill
	adrp	x8, g_vendor
	ldr	x8, [x8, :lo12:g_vendor]
	str	x19, [sp, #16]                  // 8-byte Folded Spill
	mov	x29, sp
	cbz	x8, .LBB137_5
// %bb.1:
	ldrh	w10, [x8, #10]
	cbz	x10, .LBB137_5
// %bb.2:
	mov	x11, xzr
	add	x9, x8, #20                     // =20
.LBB137_3:                              // =>This Inner Loop Header: Depth=1
	ldurh	w12, [x9, #-4]
	cmp	w12, w0
	b.eq	.LBB137_6
// %bb.4:                               //   in Loop: Header=BB137_3 Depth=1
	add	x11, x11, #1                    // =1
	cmp	x11, x10
	add	x9, x9, #8                      // =8
	b.lo	.LBB137_3
.LBB137_5:
	mov	w19, #-1
	b	.LBB137_7
.LBB137_6:
	ldrh	w10, [x9]
	ldurh	w9, [x9, #-2]
	mov	x0, x1
	cmp	w10, w2
	add	x8, x8, x9
	csel	w19, w10, w2, lo
	add	x8, x8, #1024                   // =1024
	mov	x1, x8
	mov	x2, x19
	bl	memcpy
.LBB137_7:
	mov	w0, w19
	ldr	x19, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #32             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end137:
	.size	rk_sftl_vendor_read, .Lfunc_end137-rk_sftl_vendor_read
                                        // -- End function
	.globl	rk_sftl_vendor_write            // -- Begin function rk_sftl_vendor_write
	.p2align	2
	.type	rk_sftl_vendor_write,@function
rk_sftl_vendor_write:                   // @rk_sftl_vendor_write
// %bb.0:
	hint	#25
	sub	sp, sp, #112                    // =112
	adrp	x8, g_vendor
	stp	x20, x19, [sp, #96]             // 16-byte Folded Spill
	ldr	x19, [x8, :lo12:g_vendor]
	stp	x29, x30, [sp, #16]             // 16-byte Folded Spill
	stp	x28, x27, [sp, #32]             // 16-byte Folded Spill
	stp	x26, x25, [sp, #48]             // 16-byte Folded Spill
	stp	x24, x23, [sp, #64]             // 16-byte Folded Spill
	stp	x22, x21, [sp, #80]             // 16-byte Folded Spill
	add	x29, sp, #16                    // =16
	cbz	x19, .LBB138_6
// %bb.1:
	ldrh	w23, [x19, #10]
	ldrh	w24, [x19, #8]
	add	w8, w2, #63                     // =63
	mov	w21, w2
	mov	w22, w0
	and	w25, w8, #0xffffffc0
	cbz	w23, .LBB138_5
// %bb.2:
	mov	x27, xzr
	mov	w9, w23
	sub	w8, w23, #1                     // =1
	add	x26, x19, #28                   // =28
.LBB138_3:                              // =>This Inner Loop Header: Depth=1
	ldurh	w10, [x26, #-12]
	cmp	w10, w22
	b.eq	.LBB138_8
// %bb.4:                               //   in Loop: Header=BB138_3 Depth=1
	add	x27, x27, #1                    // =1
	cmp	x9, x27
	add	x26, x26, #8                    // =8
	b.ne	.LBB138_3
.LBB138_5:
	ldrh	w8, [x19, #14]
	cmp	w25, w8
	b.ls	.LBB138_7
.LBB138_6:
	mov	w0, #-1
	b	.LBB138_18
.LBB138_7:
	add	x9, x19, x23, lsl #3
	strh	w22, [x9, #16]
	ldrh	w10, [x19, #12]
	sub	w8, w8, w25
	strh	w21, [x9, #20]
	mov	w2, w21
	strh	w10, [x9, #18]
	strh	w8, [x19, #14]
	add	x8, x19, x10
	add	w9, w10, w25
	add	x0, x8, #1024                   // =1024
	strh	w9, [x19, #12]
	bl	memcpy
	ldr	w9, [x19, #4]
	add	w8, w23, #1                     // =1
	add	w11, w24, #1                    // =1
	strh	w8, [x19, #10]
	and	w8, w11, #0xffff
	cmp	w8, #1                          // =1
	mov	w10, #65532
	csinc	w8, wzr, w24, hi
	add	w9, w9, #1                      // =1
	str	w9, [x19, #4]
	str	w9, [x19, x10]
	strh	w8, [x19, #8]
	b	.LBB138_17
.LBB138_8:
	ldurh	w9, [x26, #-8]
	add	w9, w9, #63                     // =63
	and	w20, w9, #0x1ffc0
	cmp	w20, w21
	b.hs	.LBB138_15
// %bb.9:
	ldrh	w23, [x19, #14]
	cmp	w25, w23
	b.hi	.LBB138_6
// %bb.10:
	ldurh	w28, [x26, #-10]
	cmp	w8, w27
	b.ls	.LBB138_14
// %bb.11:
	stp	w23, w20, [sp]                  // 8-byte Folded Spill
	sub	x20, x8, x27
	mov	w27, w8
	str	x1, [sp, #8]                    // 8-byte Folded Spill
.LBB138_12:                             // =>This Inner Loop Header: Depth=1
	ldurh	w8, [x26, #-4]
	ldrh	w9, [x26]
	ldurh	w11, [x26, #-2]
	add	x10, x19, #1024                 // =1024
	sturh	w8, [x26, #-12]
	add	w8, w9, #63                     // =63
	and	x23, x8, #0x1ffc0
	add	x0, x10, w28, uxtw
	add	x1, x10, x11
	mov	x2, x23
	sturh	w28, [x26, #-10]
	sturh	w9, [x26, #-8]
	bl	memcpy
	add	w28, w23, w28
	subs	x20, x20, #1                    // =1
	add	x26, x26, #8                    // =8
	b.ne	.LBB138_12
// %bb.13:
	ldr	x1, [sp, #8]                    // 8-byte Folded Reload
	ldp	w23, w20, [sp]                  // 8-byte Folded Reload
.LBB138_14:
	add	x8, x19, x27, lsl #3
	and	w9, w28, #0xffff
	strh	w22, [x8, #16]
	strh	w28, [x8, #18]
	strh	w21, [x8, #20]
	add	x8, x19, x9
	add	x0, x8, #1024                   // =1024
	mov	w2, w21
	bl	memcpy
	add	w8, w28, w25
	sub	w9, w20, w25
	strh	w8, [x19, #12]
	add	w8, w23, w9
	strh	w8, [x19, #14]
	b	.LBB138_16
.LBB138_15:
	ldurh	w8, [x26, #-10]
	mov	w2, w21
	add	x8, x19, x8
	add	x0, x8, #1024                   // =1024
	bl	memcpy
	sturh	w21, [x26, #-8]
.LBB138_16:
	ldr	w8, [x19, #4]
	add	w10, w24, #1                    // =1
	and	w10, w10, #0xffff
	cmp	w10, #1                         // =1
	mov	w9, #65532
	csinc	w10, wzr, w24, hi
	add	w8, w8, #1                      // =1
	str	w8, [x19, #4]
	str	w8, [x19, x9]
	strh	w10, [x19, #8]
.LBB138_17:
	adrp	x8, _flash_write
	ldr	x8, [x8, :lo12:_flash_write]
	lsl	w0, w24, #7
	mov	w1, #128
	mov	x2, x19
	blr	x8
	mov	w0, wzr
.LBB138_18:
	ldp	x20, x19, [sp, #96]             // 16-byte Folded Reload
	ldp	x22, x21, [sp, #80]             // 16-byte Folded Reload
	ldp	x24, x23, [sp, #64]             // 16-byte Folded Reload
	ldp	x26, x25, [sp, #48]             // 16-byte Folded Reload
	ldp	x28, x27, [sp, #32]             // 16-byte Folded Reload
	ldp	x29, x30, [sp, #16]             // 16-byte Folded Reload
	add	sp, sp, #112                    // =112
	hint	#29
	ret
.Lfunc_end138:
	.size	rk_sftl_vendor_write, .Lfunc_end138-rk_sftl_vendor_write
                                        // -- End function
	.globl	rk_sftl_vendor_storage_ioctl    // -- Begin function rk_sftl_vendor_storage_ioctl
	.p2align	2
	.type	rk_sftl_vendor_storage_ioctl,@function
rk_sftl_vendor_storage_ioctl:           // @rk_sftl_vendor_storage_ioctl
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-48]!           // 16-byte Folded Spill
	adrp	x8, kmalloc_caches+96
	ldr	x0, [x8, :lo12:kmalloc_caches+96]
	str	x21, [sp, #16]                  // 8-byte Folded Spill
	stp	x20, x19, [sp, #32]             // 16-byte Folded Spill
	mov	x20, x2
	mov	w21, w1
	mov	w1, #3265
	mov	w2, #4096
	mov	x29, sp
	bl	kmem_cache_alloc_trace
	cbz	x0, .LBB139_5
// %bb.1:
	mov	w8, #30210
	movk	w8, #16388, lsl #16
	mov	x19, x0
	cmp	w21, w8
	b.eq	.LBB139_6
// %bb.2:
	mov	w8, #30209
	movk	w8, #16388, lsl #16
	cmp	w21, w8
	b.ne	.LBB139_9
// %bb.3:
	mov	w2, #8
	mov	x0, x19
	mov	x1, x20
	bl	_copy_from_user
	cbz	x0, .LBB139_10
// %bb.4:
	adrp	x0, .L.str.131
	add	x0, x0, :lo12:.L.str.131
	mov	w1, #256
	b	.LBB139_8
.LBB139_5:
	mov	x21, #-1
	b	.LBB139_22
.LBB139_6:
	mov	w2, #8
	mov	x0, x19
	mov	x1, x20
	bl	_copy_from_user
	cbz	x0, .LBB139_16
// %bb.7:
	adrp	x0, .L.str.131
	add	x0, x0, :lo12:.L.str.131
	mov	w1, #276
.LBB139_8:
	mov	x2, x20
	bl	sftl_printk
.LBB139_9:
	mov	x21, #-14
	b	.LBB139_21
.LBB139_10:
	ldr	w8, [x19]
	mov	w9, #17745
	movk	w9, #22098, lsl #16
	cmp	w8, w9
	b.ne	.LBB139_20
// %bb.11:
	adrp	x8, g_vendor
	ldr	x8, [x8, :lo12:g_vendor]
	cbz	x8, .LBB139_20
// %bb.12:
	ldrh	w9, [x8, #10]
	cbz	x9, .LBB139_20
// %bb.13:
	ldrh	w12, [x19, #4]
	ldrh	w10, [x19, #6]
	add	x0, x19, #8                     // =8
	add	x11, x8, #20                    // =20
	mov	x21, #-1
.LBB139_14:                             // =>This Inner Loop Header: Depth=1
	ldurh	w13, [x11, #-4]
	cmp	w13, w12
	b.eq	.LBB139_23
// %bb.15:                              //   in Loop: Header=BB139_14 Depth=1
	add	x11, x11, #8                    // =8
	subs	x9, x9, #1                      // =1
	b.ne	.LBB139_14
	b	.LBB139_21
.LBB139_16:
	ldr	w8, [x19]
	mov	w9, #17745
	movk	w9, #22098, lsl #16
	cmp	w8, w9
	b.ne	.LBB139_20
// %bb.17:
	ldrh	w8, [x19, #6]
	cmp	x8, #4087                       // =4087
	b.hi	.LBB139_20
// %bb.18:
	add	x21, x8, #8                     // =8
	mov	x0, x19
	mov	x1, x21
	mov	w2, wzr
	bl	__check_object_size
	mov	x0, x19
	mov	x1, x20
	mov	x2, x21
	bl	_copy_from_user
	cbz	x0, .LBB139_24
// %bb.19:
	adrp	x0, .L.str.131
	add	x0, x0, :lo12:.L.str.131
	mov	w1, #283
	b	.LBB139_8
.LBB139_20:
	mov	x21, #-1
.LBB139_21:
	mov	x0, x19
	bl	kfree
.LBB139_22:
	mov	x0, x21
	ldp	x20, x19, [sp, #32]             // 16-byte Folded Reload
	ldr	x21, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #48             // 16-byte Folded Reload
	hint	#29
	ret
.LBB139_23:
	ldrh	w9, [x11]
	ldurh	w11, [x11, #-2]
	cmp	w9, w10
	add	x8, x8, x11
	csel	w21, w9, w10, lo
	add	x1, x8, #1024                   // =1024
	mov	x2, x21
	bl	memcpy
	strh	w21, [x19, #6]
	add	w21, w21, #8                    // =8
	mov	w2, #1
	mov	x0, x19
	mov	x1, x21
	bl	__check_object_size
	mov	x0, x20
	mov	x1, x19
	mov	x2, x21
	bl	_copy_to_user
	cmp	x0, #0                          // =0
	mov	x8, #-14
	csel	x21, xzr, x8, eq
	b	.LBB139_21
.LBB139_24:
	ldrh	w0, [x19, #4]
	ldrh	w2, [x19, #6]
	add	x1, x19, #8                     // =8
	bl	rk_sftl_vendor_write
                                        // kill: def $w0 killed $w0 def $x0
	sxtw	x21, w0
	b	.LBB139_21
.Lfunc_end139:
	.size	rk_sftl_vendor_storage_ioctl, .Lfunc_end139-rk_sftl_vendor_storage_ioctl
                                        // -- End function
	.globl	rk_sftl_vendor_register         // -- Begin function rk_sftl_vendor_register
	.p2align	2
	.type	rk_sftl_vendor_register,@function
rk_sftl_vendor_register:                // @rk_sftl_vendor_register
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	adrp	x0, rkflash_vender_storage_dev
	add	x0, x0, :lo12:rkflash_vender_storage_dev
	mov	x29, sp
	bl	misc_register
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Lfunc_end140:
	.size	rk_sftl_vendor_register, .Lfunc_end140-rk_sftl_vendor_register
                                        // -- End function
	.p2align	2                               // -- Begin function _copy_from_user
	.type	_copy_from_user,@function
_copy_from_user:                        // @_copy_from_user
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-48]!           // 16-byte Folded Spill
	//APP
	mrs	x8, SP_EL0
	//NO_APP
	ldrb	w11, [x8, #54]
	ldr	x10, [x8, #8]
	stp	x20, x19, [sp, #32]             // 16-byte Folded Spill
	mov	x19, x2
	mov	x20, x0
	lsl	x9, x1, #8
	str	x21, [sp, #16]                  // 8-byte Folded Spill
	mov	x29, sp
	tbnz	w11, #5, .LBB141_2
// %bb.1:
	ldr	x12, [x8]
	mov	x11, x1
	tbz	w12, #26, .LBB141_3
.LBB141_2:
	and	x11, x1, x9, asr #8
.LBB141_3:
	mov	x21, x19
	//APP
	adds	x11, x11, x19
	csel	x10, xzr, x10, hi
	csinv	x11, x11, xzr, lo
	sbcs	xzr, x11, x10
	cset	x11, ls

	//NO_APP
	cbz	x11, .LBB141_18
// %bb.4:
	//APP
.Ltmp1:
	b	.Ltmp0
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp2:
	.word	.Ltmp1-.Ltmp2
.Ltmp3:
	.word	.Ltmp0-.Ltmp3
.Ltmp4:
	.xword	(arm64_const_caps_ready+1)-.Ltmp4
	.text


	//NO_APP
// %bb.5:
	//APP
.Ltmp6:
	nop
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp7:
	.word	.Ltmp6-.Ltmp7
.Ltmp8:
	.word	.Ltmp5-.Ltmp8
.Ltmp9:
	.xword	(cpu_hwcap_keys+64)-.Ltmp9
	.text


	//NO_APP
.LBB141_6:
	//APP
.Ltmp10:
	mrs	x10, DAIF
.Ltmp11:
	.section	.altinstructions,"a",@progbits
.Ltmp12:
	.word	.Ltmp10-.Ltmp12
.Ltmp14:
	.word	.Ltmp13-.Ltmp14
	.hword	42
	.byte	.Ltmp11-.Ltmp10
	.byte	.Ltmp15-.Ltmp13
	.text

	.text	1
.Ltmp13:
.set .L__reg_num_x0, 0
.set .L__reg_num_x1, 1
.set .L__reg_num_x2, 2
.set .L__reg_num_x3, 3
.set .L__reg_num_x4, 4
.set .L__reg_num_x5, 5
.set .L__reg_num_x6, 6
.set .L__reg_num_x7, 7
.set .L__reg_num_x8, 8
.set .L__reg_num_x9, 9
.set .L__reg_num_x10, 10
.set .L__reg_num_x11, 11
.set .L__reg_num_x12, 12
.set .L__reg_num_x13, 13
.set .L__reg_num_x14, 14
.set .L__reg_num_x15, 15
.set .L__reg_num_x16, 16
.set .L__reg_num_x17, 17
.set .L__reg_num_x18, 18
.set .L__reg_num_x19, 19
.set .L__reg_num_x20, 20
.set .L__reg_num_x21, 21
.set .L__reg_num_x22, 22
.set .L__reg_num_x23, 23
.set .L__reg_num_x24, 24
.set .L__reg_num_x25, 25
.set .L__reg_num_x26, 26
.set .L__reg_num_x27, 27
.set .L__reg_num_x28, 28
.set .L__reg_num_x29, 29
.set .L__reg_num_x30, 30

.set .L__reg_num_xzr, 31

	.inst	0xd538460a


.Ltmp15:
.Ltmp16:
.org (.Ltmp16-(.Ltmp15-.Ltmp13))+(.Ltmp11-.Ltmp10), 0
.Ltmp17:
.org (.Ltmp17-(.Ltmp11-.Ltmp10))+(.Ltmp15-.Ltmp13), 0
	.text


	//NO_APP
	//APP
.Ltmp18:
	and	w11, w10, #0x80
.Ltmp19:
	.section	.altinstructions,"a",@progbits
.Ltmp20:
	.word	.Ltmp18-.Ltmp20
.Ltmp22:
	.word	.Ltmp21-.Ltmp22
	.hword	42
	.byte	.Ltmp19-.Ltmp18
	.byte	.Ltmp23-.Ltmp21
	.text

	.text	1
.Ltmp21:
	eor	w11, w10, #0xe0
.Ltmp23:
.Ltmp24:
.org (.Ltmp24-(.Ltmp23-.Ltmp21))+(.Ltmp19-.Ltmp18), 0
.Ltmp25:
.org (.Ltmp25-(.Ltmp19-.Ltmp18))+(.Ltmp23-.Ltmp21), 0
	.text


	//NO_APP
	cbnz	w11, .LBB141_10
// %bb.7:
	//APP
.Ltmp27:
	nop
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp28:
	.word	.Ltmp27-.Ltmp28
.Ltmp29:
	.word	.Ltmp26-.Ltmp29
.Ltmp30:
	.xword	gic_nonsecure_priorities-.Ltmp30
	.text


	//NO_APP
// %bb.8:
	mov	w11, #96
.LBB141_9:
	//APP
.Ltmp31:
	msr	DAIFSet, #2	// arch_local_irq_disable
.Ltmp32:
	.section	.altinstructions,"a",@progbits
.Ltmp33:
	.word	.Ltmp31-.Ltmp33
.Ltmp35:
	.word	.Ltmp34-.Ltmp35
	.hword	42
	.byte	.Ltmp32-.Ltmp31
	.byte	.Ltmp36-.Ltmp34
	.text

	.text	1
.Ltmp34:
.set .L__reg_num_x0, 0
.set .L__reg_num_x1, 1
.set .L__reg_num_x2, 2
.set .L__reg_num_x3, 3
.set .L__reg_num_x4, 4
.set .L__reg_num_x5, 5
.set .L__reg_num_x6, 6
.set .L__reg_num_x7, 7
.set .L__reg_num_x8, 8
.set .L__reg_num_x9, 9
.set .L__reg_num_x10, 10
.set .L__reg_num_x11, 11
.set .L__reg_num_x12, 12
.set .L__reg_num_x13, 13
.set .L__reg_num_x14, 14
.set .L__reg_num_x15, 15
.set .L__reg_num_x16, 16
.set .L__reg_num_x17, 17
.set .L__reg_num_x18, 18
.set .L__reg_num_x19, 19
.set .L__reg_num_x20, 20
.set .L__reg_num_x21, 21
.set .L__reg_num_x22, 22
.set .L__reg_num_x23, 23
.set .L__reg_num_x24, 24
.set .L__reg_num_x25, 25
.set .L__reg_num_x26, 26
.set .L__reg_num_x27, 27
.set .L__reg_num_x28, 28
.set .L__reg_num_x29, 29
.set .L__reg_num_x30, 30

.set .L__reg_num_xzr, 31

	.inst	0xd518460b


.Ltmp36:
.Ltmp37:
.org (.Ltmp37-(.Ltmp36-.Ltmp34))+(.Ltmp32-.Ltmp31), 0
.Ltmp38:
.org (.Ltmp38-(.Ltmp32-.Ltmp31))+(.Ltmp36-.Ltmp34), 0
	.text


	//NO_APP
.LBB141_10:
	ldr	x11, [x8, #16]
	//APP
	mrs	x12, TTBR1_EL1
	//NO_APP
	mov	x13, x11
	bfxil	x13, x12, #0, #48
	//APP
	msr	TTBR1_EL1, x13
	//NO_APP
	//APP
	isb
	//NO_APP
	//APP
	msr	TTBR0_EL1, x11
	//NO_APP
	//APP
	isb
	//NO_APP
	//APP
.Ltmp39:
	msr	DAIF, x10
.Ltmp40:
	.section	.altinstructions,"a",@progbits
.Ltmp41:
	.word	.Ltmp39-.Ltmp41
.Ltmp43:
	.word	.Ltmp42-.Ltmp43
	.hword	42
	.byte	.Ltmp40-.Ltmp39
	.byte	.Ltmp44-.Ltmp42
	.text

	.text	1
.Ltmp42:
.set .L__reg_num_x0, 0
.set .L__reg_num_x1, 1
.set .L__reg_num_x2, 2
.set .L__reg_num_x3, 3
.set .L__reg_num_x4, 4
.set .L__reg_num_x5, 5
.set .L__reg_num_x6, 6
.set .L__reg_num_x7, 7
.set .L__reg_num_x8, 8
.set .L__reg_num_x9, 9
.set .L__reg_num_x10, 10
.set .L__reg_num_x11, 11
.set .L__reg_num_x12, 12
.set .L__reg_num_x13, 13
.set .L__reg_num_x14, 14
.set .L__reg_num_x15, 15
.set .L__reg_num_x16, 16
.set .L__reg_num_x17, 17
.set .L__reg_num_x18, 18
.set .L__reg_num_x19, 19
.set .L__reg_num_x20, 20
.set .L__reg_num_x21, 21
.set .L__reg_num_x22, 22
.set .L__reg_num_x23, 23
.set .L__reg_num_x24, 24
.set .L__reg_num_x25, 25
.set .L__reg_num_x26, 26
.set .L__reg_num_x27, 27
.set .L__reg_num_x28, 28
.set .L__reg_num_x29, 29
.set .L__reg_num_x30, 30

.set .L__reg_num_xzr, 31

	.inst	0xd518460a


.Ltmp44:
.Ltmp45:
.org (.Ltmp45-(.Ltmp44-.Ltmp42))+(.Ltmp40-.Ltmp39), 0
.Ltmp46:
.org (.Ltmp46-(.Ltmp40-.Ltmp39))+(.Ltmp44-.Ltmp42), 0
	.text


	//NO_APP
	//APP
.Ltmp48:
	nop
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp49:
	.word	.Ltmp48-.Ltmp49
.Ltmp50:
	.word	.Ltmp47-.Ltmp50
.Ltmp51:
	.xword	gic_pmr_sync-.Ltmp51
	.text


	//NO_APP
.LBB141_11:
	ldr	x10, [x8, #8]
	and	x9, x1, x9, asr #8
	//APP
	bics	xzr, x9, x10
	csel	x8, x1, xzr, eq

	//NO_APP
	//APP
	csdb
	//NO_APP
	mov	x0, x20
	mov	x1, x8
	mov	x2, x19
	bl	__arch_copy_from_user
	mov	x21, x0
	//APP
.Ltmp53:
	b	.Ltmp52
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp54:
	.word	.Ltmp53-.Ltmp54
.Ltmp55:
	.word	.Ltmp52-.Ltmp55
.Ltmp56:
	.xword	(arm64_const_caps_ready+1)-.Ltmp56
	.text


	//NO_APP
// %bb.12:
	//APP
.Ltmp58:
	nop
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp59:
	.word	.Ltmp58-.Ltmp59
.Ltmp60:
	.word	.Ltmp57-.Ltmp60
.Ltmp61:
	.xword	(cpu_hwcap_keys+64)-.Ltmp61
	.text


	//NO_APP
.LBB141_13:
	//APP
.Ltmp62:
	mrs	x8, DAIF
.Ltmp63:
	.section	.altinstructions,"a",@progbits
.Ltmp64:
	.word	.Ltmp62-.Ltmp64
.Ltmp66:
	.word	.Ltmp65-.Ltmp66
	.hword	42
	.byte	.Ltmp63-.Ltmp62
	.byte	.Ltmp67-.Ltmp65
	.text

	.text	1
.Ltmp65:
.set .L__reg_num_x0, 0
.set .L__reg_num_x1, 1
.set .L__reg_num_x2, 2
.set .L__reg_num_x3, 3
.set .L__reg_num_x4, 4
.set .L__reg_num_x5, 5
.set .L__reg_num_x6, 6
.set .L__reg_num_x7, 7
.set .L__reg_num_x8, 8
.set .L__reg_num_x9, 9
.set .L__reg_num_x10, 10
.set .L__reg_num_x11, 11
.set .L__reg_num_x12, 12
.set .L__reg_num_x13, 13
.set .L__reg_num_x14, 14
.set .L__reg_num_x15, 15
.set .L__reg_num_x16, 16
.set .L__reg_num_x17, 17
.set .L__reg_num_x18, 18
.set .L__reg_num_x19, 19
.set .L__reg_num_x20, 20
.set .L__reg_num_x21, 21
.set .L__reg_num_x22, 22
.set .L__reg_num_x23, 23
.set .L__reg_num_x24, 24
.set .L__reg_num_x25, 25
.set .L__reg_num_x26, 26
.set .L__reg_num_x27, 27
.set .L__reg_num_x28, 28
.set .L__reg_num_x29, 29
.set .L__reg_num_x30, 30

.set .L__reg_num_xzr, 31

	.inst	0xd5384608


.Ltmp67:
.Ltmp68:
.org (.Ltmp68-(.Ltmp67-.Ltmp65))+(.Ltmp63-.Ltmp62), 0
.Ltmp69:
.org (.Ltmp69-(.Ltmp63-.Ltmp62))+(.Ltmp67-.Ltmp65), 0
	.text


	//NO_APP
	//APP
.Ltmp70:
	and	w9, w8, #0x80
.Ltmp71:
	.section	.altinstructions,"a",@progbits
.Ltmp72:
	.word	.Ltmp70-.Ltmp72
.Ltmp74:
	.word	.Ltmp73-.Ltmp74
	.hword	42
	.byte	.Ltmp71-.Ltmp70
	.byte	.Ltmp75-.Ltmp73
	.text

	.text	1
.Ltmp73:
	eor	w9, w8, #0xe0
.Ltmp75:
.Ltmp76:
.org (.Ltmp76-(.Ltmp75-.Ltmp73))+(.Ltmp71-.Ltmp70), 0
.Ltmp77:
.org (.Ltmp77-(.Ltmp71-.Ltmp70))+(.Ltmp75-.Ltmp73), 0
	.text


	//NO_APP
	cbnz	w9, .LBB141_17
// %bb.14:
	//APP
.Ltmp79:
	nop
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp80:
	.word	.Ltmp79-.Ltmp80
.Ltmp81:
	.word	.Ltmp78-.Ltmp81
.Ltmp82:
	.xword	gic_nonsecure_priorities-.Ltmp82
	.text


	//NO_APP
// %bb.15:
	mov	w9, #96
.LBB141_16:
	//APP
.Ltmp83:
	msr	DAIFSet, #2	// arch_local_irq_disable
.Ltmp84:
	.section	.altinstructions,"a",@progbits
.Ltmp85:
	.word	.Ltmp83-.Ltmp85
.Ltmp87:
	.word	.Ltmp86-.Ltmp87
	.hword	42
	.byte	.Ltmp84-.Ltmp83
	.byte	.Ltmp88-.Ltmp86
	.text

	.text	1
.Ltmp86:
.set .L__reg_num_x0, 0
.set .L__reg_num_x1, 1
.set .L__reg_num_x2, 2
.set .L__reg_num_x3, 3
.set .L__reg_num_x4, 4
.set .L__reg_num_x5, 5
.set .L__reg_num_x6, 6
.set .L__reg_num_x7, 7
.set .L__reg_num_x8, 8
.set .L__reg_num_x9, 9
.set .L__reg_num_x10, 10
.set .L__reg_num_x11, 11
.set .L__reg_num_x12, 12
.set .L__reg_num_x13, 13
.set .L__reg_num_x14, 14
.set .L__reg_num_x15, 15
.set .L__reg_num_x16, 16
.set .L__reg_num_x17, 17
.set .L__reg_num_x18, 18
.set .L__reg_num_x19, 19
.set .L__reg_num_x20, 20
.set .L__reg_num_x21, 21
.set .L__reg_num_x22, 22
.set .L__reg_num_x23, 23
.set .L__reg_num_x24, 24
.set .L__reg_num_x25, 25
.set .L__reg_num_x26, 26
.set .L__reg_num_x27, 27
.set .L__reg_num_x28, 28
.set .L__reg_num_x29, 29
.set .L__reg_num_x30, 30

.set .L__reg_num_xzr, 31

	.inst	0xd5184609


.Ltmp88:
.Ltmp89:
.org (.Ltmp89-(.Ltmp88-.Ltmp86))+(.Ltmp84-.Ltmp83), 0
.Ltmp90:
.org (.Ltmp90-(.Ltmp84-.Ltmp83))+(.Ltmp88-.Ltmp86), 0
	.text


	//NO_APP
.LBB141_17:
	//APP
	mrs	x9, TTBR1_EL1
	//NO_APP
	and	x9, x9, #0xffffffffffff
	sub	x10, x9, #1, lsl #12            // =4096
	//APP
	msr	TTBR0_EL1, x10
	//NO_APP
	//APP
	isb
	//NO_APP
	//APP
	msr	TTBR1_EL1, x9
	//NO_APP
	//APP
	isb
	//NO_APP
	//APP
.Ltmp91:
	msr	DAIF, x8
.Ltmp92:
	.section	.altinstructions,"a",@progbits
.Ltmp93:
	.word	.Ltmp91-.Ltmp93
.Ltmp95:
	.word	.Ltmp94-.Ltmp95
	.hword	42
	.byte	.Ltmp92-.Ltmp91
	.byte	.Ltmp96-.Ltmp94
	.text

	.text	1
.Ltmp94:
.set .L__reg_num_x0, 0
.set .L__reg_num_x1, 1
.set .L__reg_num_x2, 2
.set .L__reg_num_x3, 3
.set .L__reg_num_x4, 4
.set .L__reg_num_x5, 5
.set .L__reg_num_x6, 6
.set .L__reg_num_x7, 7
.set .L__reg_num_x8, 8
.set .L__reg_num_x9, 9
.set .L__reg_num_x10, 10
.set .L__reg_num_x11, 11
.set .L__reg_num_x12, 12
.set .L__reg_num_x13, 13
.set .L__reg_num_x14, 14
.set .L__reg_num_x15, 15
.set .L__reg_num_x16, 16
.set .L__reg_num_x17, 17
.set .L__reg_num_x18, 18
.set .L__reg_num_x19, 19
.set .L__reg_num_x20, 20
.set .L__reg_num_x21, 21
.set .L__reg_num_x22, 22
.set .L__reg_num_x23, 23
.set .L__reg_num_x24, 24
.set .L__reg_num_x25, 25
.set .L__reg_num_x26, 26
.set .L__reg_num_x27, 27
.set .L__reg_num_x28, 28
.set .L__reg_num_x29, 29
.set .L__reg_num_x30, 30

.set .L__reg_num_xzr, 31

	.inst	0xd5184608


.Ltmp96:
.Ltmp97:
.org (.Ltmp97-(.Ltmp96-.Ltmp94))+(.Ltmp92-.Ltmp91), 0
.Ltmp98:
.org (.Ltmp98-(.Ltmp92-.Ltmp91))+(.Ltmp96-.Ltmp94), 0
	.text


	//NO_APP
	//APP
.Ltmp100:
	nop
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp101:
	.word	.Ltmp100-.Ltmp101
.Ltmp102:
	.word	.Ltmp99-.Ltmp102
.Ltmp103:
	.xword	gic_pmr_sync-.Ltmp103
	.text


	//NO_APP
.LBB141_18:
	cbnz	x21, .LBB141_20
.LBB141_19:
	mov	x0, x21
	ldp	x20, x19, [sp, #32]             // 16-byte Folded Reload
	ldr	x21, [sp, #16]                  // 8-byte Folded Reload
	ldp	x29, x30, [sp], #48             // 16-byte Folded Reload
	hint	#29
	ret
.LBB141_20:
	sub	x8, x19, x21
	add	x0, x20, x8
	mov	w1, wzr
	mov	x2, x21
	bl	memset
	b	.LBB141_19
.Ltmp0:                                 // Block address taken
.LBB141_21:
	hint	#36
	adrp	x10, cpu_hwcaps
	ldr	x10, [x10, :lo12:cpu_hwcaps]
	tbz	w10, #4, .LBB141_6
.Ltmp5:                                 // Block address taken
.LBB141_22:
	hint	#36
	//APP
.Ltmp104:
	nop
.Ltmp105:
	.section	.altinstructions,"a",@progbits
.Ltmp106:
	.word	.Ltmp104-.Ltmp106
.Ltmp108:
	.word	.Ltmp107-.Ltmp108
	.hword	10
	.byte	.Ltmp105-.Ltmp104
	.byte	.Ltmp109-.Ltmp107
	.text

	.text	1
.Ltmp107:
	.inst	0xd500409f

.Ltmp109:
.Ltmp110:
.org (.Ltmp110-(.Ltmp109-.Ltmp107))+(.Ltmp105-.Ltmp104), 0
.Ltmp111:
.org (.Ltmp111-(.Ltmp105-.Ltmp104))+(.Ltmp109-.Ltmp107), 0
	.text


	//NO_APP
	b	.LBB141_11
.Ltmp47:                                // Block address taken
.LBB141_23:
	hint	#36
	//APP
	dsb	sy
	//NO_APP
	b	.LBB141_11
.Ltmp52:                                // Block address taken
.LBB141_24:
	hint	#36
	adrp	x8, cpu_hwcaps
	ldr	x8, [x8, :lo12:cpu_hwcaps]
	tbz	w8, #4, .LBB141_13
.Ltmp57:                                // Block address taken
.LBB141_25:
	hint	#36
	//APP
.Ltmp112:
	nop
.Ltmp113:
	.section	.altinstructions,"a",@progbits
.Ltmp114:
	.word	.Ltmp112-.Ltmp114
.Ltmp116:
	.word	.Ltmp115-.Ltmp116
	.hword	10
	.byte	.Ltmp113-.Ltmp112
	.byte	.Ltmp117-.Ltmp115
	.text

	.text	1
.Ltmp115:
	.inst	0xd500419f

.Ltmp117:
.Ltmp118:
.org (.Ltmp118-(.Ltmp117-.Ltmp115))+(.Ltmp113-.Ltmp112), 0
.Ltmp119:
.org (.Ltmp119-(.Ltmp113-.Ltmp112))+(.Ltmp117-.Ltmp115), 0
	.text


	//NO_APP
	b	.LBB141_18
.Ltmp99:                                // Block address taken
.LBB141_26:
	hint	#36
	//APP
	dsb	sy
	//NO_APP
	b	.LBB141_18
.Ltmp26:                                // Block address taken
.LBB141_27:
	hint	#36
	mov	w11, #160
	b	.LBB141_9
.Ltmp78:                                // Block address taken
.LBB141_28:
	hint	#36
	mov	w9, #160
	b	.LBB141_16
.Lfunc_end141:
	.size	_copy_from_user, .Lfunc_end141-_copy_from_user
                                        // -- End function
	.p2align	2                               // -- Begin function _copy_to_user
	.type	_copy_to_user,@function
_copy_to_user:                          // @_copy_to_user
// %bb.0:
	hint	#25
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	//APP
	mrs	x8, SP_EL0
	//NO_APP
	ldrb	w11, [x8, #54]
	ldr	x10, [x8, #8]
	lsl	x9, x0, #8
	mov	x29, sp
	tbnz	w11, #5, .LBB142_2
// %bb.1:
	ldr	x12, [x8]
	mov	x11, x0
	tbz	w12, #26, .LBB142_3
.LBB142_2:
	and	x11, x0, x9, asr #8
.LBB142_3:
	//APP
	adds	x11, x11, x2
	csel	x10, xzr, x10, hi
	csinv	x11, x11, xzr, lo
	sbcs	xzr, x11, x10
	cset	x11, ls

	//NO_APP
	cbz	x11, .LBB142_18
// %bb.4:
	//APP
.Ltmp121:
	b	.Ltmp120
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp122:
	.word	.Ltmp121-.Ltmp122
.Ltmp123:
	.word	.Ltmp120-.Ltmp123
.Ltmp124:
	.xword	(arm64_const_caps_ready+1)-.Ltmp124
	.text


	//NO_APP
// %bb.5:
	//APP
.Ltmp126:
	nop
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp127:
	.word	.Ltmp126-.Ltmp127
.Ltmp128:
	.word	.Ltmp125-.Ltmp128
.Ltmp129:
	.xword	(cpu_hwcap_keys+64)-.Ltmp129
	.text


	//NO_APP
.LBB142_6:
	//APP
.Ltmp130:
	mrs	x10, DAIF
.Ltmp131:
	.section	.altinstructions,"a",@progbits
.Ltmp132:
	.word	.Ltmp130-.Ltmp132
.Ltmp134:
	.word	.Ltmp133-.Ltmp134
	.hword	42
	.byte	.Ltmp131-.Ltmp130
	.byte	.Ltmp135-.Ltmp133
	.text

	.text	1
.Ltmp133:
.set .L__reg_num_x0, 0
.set .L__reg_num_x1, 1
.set .L__reg_num_x2, 2
.set .L__reg_num_x3, 3
.set .L__reg_num_x4, 4
.set .L__reg_num_x5, 5
.set .L__reg_num_x6, 6
.set .L__reg_num_x7, 7
.set .L__reg_num_x8, 8
.set .L__reg_num_x9, 9
.set .L__reg_num_x10, 10
.set .L__reg_num_x11, 11
.set .L__reg_num_x12, 12
.set .L__reg_num_x13, 13
.set .L__reg_num_x14, 14
.set .L__reg_num_x15, 15
.set .L__reg_num_x16, 16
.set .L__reg_num_x17, 17
.set .L__reg_num_x18, 18
.set .L__reg_num_x19, 19
.set .L__reg_num_x20, 20
.set .L__reg_num_x21, 21
.set .L__reg_num_x22, 22
.set .L__reg_num_x23, 23
.set .L__reg_num_x24, 24
.set .L__reg_num_x25, 25
.set .L__reg_num_x26, 26
.set .L__reg_num_x27, 27
.set .L__reg_num_x28, 28
.set .L__reg_num_x29, 29
.set .L__reg_num_x30, 30

.set .L__reg_num_xzr, 31

	.inst	0xd538460a


.Ltmp135:
.Ltmp136:
.org (.Ltmp136-(.Ltmp135-.Ltmp133))+(.Ltmp131-.Ltmp130), 0
.Ltmp137:
.org (.Ltmp137-(.Ltmp131-.Ltmp130))+(.Ltmp135-.Ltmp133), 0
	.text


	//NO_APP
	//APP
.Ltmp138:
	and	w11, w10, #0x80
.Ltmp139:
	.section	.altinstructions,"a",@progbits
.Ltmp140:
	.word	.Ltmp138-.Ltmp140
.Ltmp142:
	.word	.Ltmp141-.Ltmp142
	.hword	42
	.byte	.Ltmp139-.Ltmp138
	.byte	.Ltmp143-.Ltmp141
	.text

	.text	1
.Ltmp141:
	eor	w11, w10, #0xe0
.Ltmp143:
.Ltmp144:
.org (.Ltmp144-(.Ltmp143-.Ltmp141))+(.Ltmp139-.Ltmp138), 0
.Ltmp145:
.org (.Ltmp145-(.Ltmp139-.Ltmp138))+(.Ltmp143-.Ltmp141), 0
	.text


	//NO_APP
	cbnz	w11, .LBB142_10
// %bb.7:
	//APP
.Ltmp147:
	nop
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp148:
	.word	.Ltmp147-.Ltmp148
.Ltmp149:
	.word	.Ltmp146-.Ltmp149
.Ltmp150:
	.xword	gic_nonsecure_priorities-.Ltmp150
	.text


	//NO_APP
// %bb.8:
	mov	w11, #96
.LBB142_9:
	//APP
.Ltmp151:
	msr	DAIFSet, #2	// arch_local_irq_disable
.Ltmp152:
	.section	.altinstructions,"a",@progbits
.Ltmp153:
	.word	.Ltmp151-.Ltmp153
.Ltmp155:
	.word	.Ltmp154-.Ltmp155
	.hword	42
	.byte	.Ltmp152-.Ltmp151
	.byte	.Ltmp156-.Ltmp154
	.text

	.text	1
.Ltmp154:
.set .L__reg_num_x0, 0
.set .L__reg_num_x1, 1
.set .L__reg_num_x2, 2
.set .L__reg_num_x3, 3
.set .L__reg_num_x4, 4
.set .L__reg_num_x5, 5
.set .L__reg_num_x6, 6
.set .L__reg_num_x7, 7
.set .L__reg_num_x8, 8
.set .L__reg_num_x9, 9
.set .L__reg_num_x10, 10
.set .L__reg_num_x11, 11
.set .L__reg_num_x12, 12
.set .L__reg_num_x13, 13
.set .L__reg_num_x14, 14
.set .L__reg_num_x15, 15
.set .L__reg_num_x16, 16
.set .L__reg_num_x17, 17
.set .L__reg_num_x18, 18
.set .L__reg_num_x19, 19
.set .L__reg_num_x20, 20
.set .L__reg_num_x21, 21
.set .L__reg_num_x22, 22
.set .L__reg_num_x23, 23
.set .L__reg_num_x24, 24
.set .L__reg_num_x25, 25
.set .L__reg_num_x26, 26
.set .L__reg_num_x27, 27
.set .L__reg_num_x28, 28
.set .L__reg_num_x29, 29
.set .L__reg_num_x30, 30

.set .L__reg_num_xzr, 31

	.inst	0xd518460b


.Ltmp156:
.Ltmp157:
.org (.Ltmp157-(.Ltmp156-.Ltmp154))+(.Ltmp152-.Ltmp151), 0
.Ltmp158:
.org (.Ltmp158-(.Ltmp152-.Ltmp151))+(.Ltmp156-.Ltmp154), 0
	.text


	//NO_APP
.LBB142_10:
	ldr	x11, [x8, #16]
	//APP
	mrs	x12, TTBR1_EL1
	//NO_APP
	mov	x13, x11
	bfxil	x13, x12, #0, #48
	//APP
	msr	TTBR1_EL1, x13
	//NO_APP
	//APP
	isb
	//NO_APP
	//APP
	msr	TTBR0_EL1, x11
	//NO_APP
	//APP
	isb
	//NO_APP
	//APP
.Ltmp159:
	msr	DAIF, x10
.Ltmp160:
	.section	.altinstructions,"a",@progbits
.Ltmp161:
	.word	.Ltmp159-.Ltmp161
.Ltmp163:
	.word	.Ltmp162-.Ltmp163
	.hword	42
	.byte	.Ltmp160-.Ltmp159
	.byte	.Ltmp164-.Ltmp162
	.text

	.text	1
.Ltmp162:
.set .L__reg_num_x0, 0
.set .L__reg_num_x1, 1
.set .L__reg_num_x2, 2
.set .L__reg_num_x3, 3
.set .L__reg_num_x4, 4
.set .L__reg_num_x5, 5
.set .L__reg_num_x6, 6
.set .L__reg_num_x7, 7
.set .L__reg_num_x8, 8
.set .L__reg_num_x9, 9
.set .L__reg_num_x10, 10
.set .L__reg_num_x11, 11
.set .L__reg_num_x12, 12
.set .L__reg_num_x13, 13
.set .L__reg_num_x14, 14
.set .L__reg_num_x15, 15
.set .L__reg_num_x16, 16
.set .L__reg_num_x17, 17
.set .L__reg_num_x18, 18
.set .L__reg_num_x19, 19
.set .L__reg_num_x20, 20
.set .L__reg_num_x21, 21
.set .L__reg_num_x22, 22
.set .L__reg_num_x23, 23
.set .L__reg_num_x24, 24
.set .L__reg_num_x25, 25
.set .L__reg_num_x26, 26
.set .L__reg_num_x27, 27
.set .L__reg_num_x28, 28
.set .L__reg_num_x29, 29
.set .L__reg_num_x30, 30

.set .L__reg_num_xzr, 31

	.inst	0xd518460a


.Ltmp164:
.Ltmp165:
.org (.Ltmp165-(.Ltmp164-.Ltmp162))+(.Ltmp160-.Ltmp159), 0
.Ltmp166:
.org (.Ltmp166-(.Ltmp160-.Ltmp159))+(.Ltmp164-.Ltmp162), 0
	.text


	//NO_APP
	//APP
.Ltmp168:
	nop
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp169:
	.word	.Ltmp168-.Ltmp169
.Ltmp170:
	.word	.Ltmp167-.Ltmp170
.Ltmp171:
	.xword	gic_pmr_sync-.Ltmp171
	.text


	//NO_APP
.LBB142_11:
	ldr	x10, [x8, #8]
	and	x9, x0, x9, asr #8
	//APP
	bics	xzr, x9, x10
	csel	x8, x0, xzr, eq

	//NO_APP
	//APP
	csdb
	//NO_APP
	mov	x0, x8
	bl	__arch_copy_to_user
	mov	x2, x0
	//APP
.Ltmp173:
	b	.Ltmp172
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp174:
	.word	.Ltmp173-.Ltmp174
.Ltmp175:
	.word	.Ltmp172-.Ltmp175
.Ltmp176:
	.xword	(arm64_const_caps_ready+1)-.Ltmp176
	.text


	//NO_APP
// %bb.12:
	//APP
.Ltmp178:
	nop
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp179:
	.word	.Ltmp178-.Ltmp179
.Ltmp180:
	.word	.Ltmp177-.Ltmp180
.Ltmp181:
	.xword	(cpu_hwcap_keys+64)-.Ltmp181
	.text


	//NO_APP
.LBB142_13:
	//APP
.Ltmp182:
	mrs	x8, DAIF
.Ltmp183:
	.section	.altinstructions,"a",@progbits
.Ltmp184:
	.word	.Ltmp182-.Ltmp184
.Ltmp186:
	.word	.Ltmp185-.Ltmp186
	.hword	42
	.byte	.Ltmp183-.Ltmp182
	.byte	.Ltmp187-.Ltmp185
	.text

	.text	1
.Ltmp185:
.set .L__reg_num_x0, 0
.set .L__reg_num_x1, 1
.set .L__reg_num_x2, 2
.set .L__reg_num_x3, 3
.set .L__reg_num_x4, 4
.set .L__reg_num_x5, 5
.set .L__reg_num_x6, 6
.set .L__reg_num_x7, 7
.set .L__reg_num_x8, 8
.set .L__reg_num_x9, 9
.set .L__reg_num_x10, 10
.set .L__reg_num_x11, 11
.set .L__reg_num_x12, 12
.set .L__reg_num_x13, 13
.set .L__reg_num_x14, 14
.set .L__reg_num_x15, 15
.set .L__reg_num_x16, 16
.set .L__reg_num_x17, 17
.set .L__reg_num_x18, 18
.set .L__reg_num_x19, 19
.set .L__reg_num_x20, 20
.set .L__reg_num_x21, 21
.set .L__reg_num_x22, 22
.set .L__reg_num_x23, 23
.set .L__reg_num_x24, 24
.set .L__reg_num_x25, 25
.set .L__reg_num_x26, 26
.set .L__reg_num_x27, 27
.set .L__reg_num_x28, 28
.set .L__reg_num_x29, 29
.set .L__reg_num_x30, 30

.set .L__reg_num_xzr, 31

	.inst	0xd5384608


.Ltmp187:
.Ltmp188:
.org (.Ltmp188-(.Ltmp187-.Ltmp185))+(.Ltmp183-.Ltmp182), 0
.Ltmp189:
.org (.Ltmp189-(.Ltmp183-.Ltmp182))+(.Ltmp187-.Ltmp185), 0
	.text


	//NO_APP
	//APP
.Ltmp190:
	and	w9, w8, #0x80
.Ltmp191:
	.section	.altinstructions,"a",@progbits
.Ltmp192:
	.word	.Ltmp190-.Ltmp192
.Ltmp194:
	.word	.Ltmp193-.Ltmp194
	.hword	42
	.byte	.Ltmp191-.Ltmp190
	.byte	.Ltmp195-.Ltmp193
	.text

	.text	1
.Ltmp193:
	eor	w9, w8, #0xe0
.Ltmp195:
.Ltmp196:
.org (.Ltmp196-(.Ltmp195-.Ltmp193))+(.Ltmp191-.Ltmp190), 0
.Ltmp197:
.org (.Ltmp197-(.Ltmp191-.Ltmp190))+(.Ltmp195-.Ltmp193), 0
	.text


	//NO_APP
	cbnz	w9, .LBB142_17
// %bb.14:
	//APP
.Ltmp199:
	nop
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp200:
	.word	.Ltmp199-.Ltmp200
.Ltmp201:
	.word	.Ltmp198-.Ltmp201
.Ltmp202:
	.xword	gic_nonsecure_priorities-.Ltmp202
	.text


	//NO_APP
// %bb.15:
	mov	w9, #96
.LBB142_16:
	//APP
.Ltmp203:
	msr	DAIFSet, #2	// arch_local_irq_disable
.Ltmp204:
	.section	.altinstructions,"a",@progbits
.Ltmp205:
	.word	.Ltmp203-.Ltmp205
.Ltmp207:
	.word	.Ltmp206-.Ltmp207
	.hword	42
	.byte	.Ltmp204-.Ltmp203
	.byte	.Ltmp208-.Ltmp206
	.text

	.text	1
.Ltmp206:
.set .L__reg_num_x0, 0
.set .L__reg_num_x1, 1
.set .L__reg_num_x2, 2
.set .L__reg_num_x3, 3
.set .L__reg_num_x4, 4
.set .L__reg_num_x5, 5
.set .L__reg_num_x6, 6
.set .L__reg_num_x7, 7
.set .L__reg_num_x8, 8
.set .L__reg_num_x9, 9
.set .L__reg_num_x10, 10
.set .L__reg_num_x11, 11
.set .L__reg_num_x12, 12
.set .L__reg_num_x13, 13
.set .L__reg_num_x14, 14
.set .L__reg_num_x15, 15
.set .L__reg_num_x16, 16
.set .L__reg_num_x17, 17
.set .L__reg_num_x18, 18
.set .L__reg_num_x19, 19
.set .L__reg_num_x20, 20
.set .L__reg_num_x21, 21
.set .L__reg_num_x22, 22
.set .L__reg_num_x23, 23
.set .L__reg_num_x24, 24
.set .L__reg_num_x25, 25
.set .L__reg_num_x26, 26
.set .L__reg_num_x27, 27
.set .L__reg_num_x28, 28
.set .L__reg_num_x29, 29
.set .L__reg_num_x30, 30

.set .L__reg_num_xzr, 31

	.inst	0xd5184609


.Ltmp208:
.Ltmp209:
.org (.Ltmp209-(.Ltmp208-.Ltmp206))+(.Ltmp204-.Ltmp203), 0
.Ltmp210:
.org (.Ltmp210-(.Ltmp204-.Ltmp203))+(.Ltmp208-.Ltmp206), 0
	.text


	//NO_APP
.LBB142_17:
	//APP
	mrs	x9, TTBR1_EL1
	//NO_APP
	and	x9, x9, #0xffffffffffff
	sub	x10, x9, #1, lsl #12            // =4096
	//APP
	msr	TTBR0_EL1, x10
	//NO_APP
	//APP
	isb
	//NO_APP
	//APP
	msr	TTBR1_EL1, x9
	//NO_APP
	//APP
	isb
	//NO_APP
	//APP
.Ltmp211:
	msr	DAIF, x8
.Ltmp212:
	.section	.altinstructions,"a",@progbits
.Ltmp213:
	.word	.Ltmp211-.Ltmp213
.Ltmp215:
	.word	.Ltmp214-.Ltmp215
	.hword	42
	.byte	.Ltmp212-.Ltmp211
	.byte	.Ltmp216-.Ltmp214
	.text

	.text	1
.Ltmp214:
.set .L__reg_num_x0, 0
.set .L__reg_num_x1, 1
.set .L__reg_num_x2, 2
.set .L__reg_num_x3, 3
.set .L__reg_num_x4, 4
.set .L__reg_num_x5, 5
.set .L__reg_num_x6, 6
.set .L__reg_num_x7, 7
.set .L__reg_num_x8, 8
.set .L__reg_num_x9, 9
.set .L__reg_num_x10, 10
.set .L__reg_num_x11, 11
.set .L__reg_num_x12, 12
.set .L__reg_num_x13, 13
.set .L__reg_num_x14, 14
.set .L__reg_num_x15, 15
.set .L__reg_num_x16, 16
.set .L__reg_num_x17, 17
.set .L__reg_num_x18, 18
.set .L__reg_num_x19, 19
.set .L__reg_num_x20, 20
.set .L__reg_num_x21, 21
.set .L__reg_num_x22, 22
.set .L__reg_num_x23, 23
.set .L__reg_num_x24, 24
.set .L__reg_num_x25, 25
.set .L__reg_num_x26, 26
.set .L__reg_num_x27, 27
.set .L__reg_num_x28, 28
.set .L__reg_num_x29, 29
.set .L__reg_num_x30, 30

.set .L__reg_num_xzr, 31

	.inst	0xd5184608


.Ltmp216:
.Ltmp217:
.org (.Ltmp217-(.Ltmp216-.Ltmp214))+(.Ltmp212-.Ltmp211), 0
.Ltmp218:
.org (.Ltmp218-(.Ltmp212-.Ltmp211))+(.Ltmp216-.Ltmp214), 0
	.text


	//NO_APP
	//APP
.Ltmp220:
	nop
	.section	__jump_table,"aw",@progbits
	.p2align	3
.Ltmp221:
	.word	.Ltmp220-.Ltmp221
.Ltmp222:
	.word	.Ltmp219-.Ltmp222
.Ltmp223:
	.xword	gic_pmr_sync-.Ltmp223
	.text


	//NO_APP
.LBB142_18:
	mov	x0, x2
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	hint	#29
	ret
.Ltmp120:                               // Block address taken
.LBB142_19:
	hint	#36
	adrp	x10, cpu_hwcaps
	ldr	x10, [x10, :lo12:cpu_hwcaps]
	tbz	w10, #4, .LBB142_6
.Ltmp125:                               // Block address taken
.LBB142_20:
	hint	#36
	//APP
.Ltmp224:
	nop
.Ltmp225:
	.section	.altinstructions,"a",@progbits
.Ltmp226:
	.word	.Ltmp224-.Ltmp226
.Ltmp228:
	.word	.Ltmp227-.Ltmp228
	.hword	10
	.byte	.Ltmp225-.Ltmp224
	.byte	.Ltmp229-.Ltmp227
	.text

	.text	1
.Ltmp227:
	.inst	0xd500409f

.Ltmp229:
.Ltmp230:
.org (.Ltmp230-(.Ltmp229-.Ltmp227))+(.Ltmp225-.Ltmp224), 0
.Ltmp231:
.org (.Ltmp231-(.Ltmp225-.Ltmp224))+(.Ltmp229-.Ltmp227), 0
	.text


	//NO_APP
	b	.LBB142_11
.Ltmp167:                               // Block address taken
.LBB142_21:
	hint	#36
	//APP
	dsb	sy
	//NO_APP
	b	.LBB142_11
.Ltmp172:                               // Block address taken
.LBB142_22:
	hint	#36
	adrp	x8, cpu_hwcaps
	ldr	x8, [x8, :lo12:cpu_hwcaps]
	tbz	w8, #4, .LBB142_13
.Ltmp177:                               // Block address taken
.LBB142_23:
	hint	#36
	//APP
.Ltmp232:
	nop
.Ltmp233:
	.section	.altinstructions,"a",@progbits
.Ltmp234:
	.word	.Ltmp232-.Ltmp234
.Ltmp236:
	.word	.Ltmp235-.Ltmp236
	.hword	10
	.byte	.Ltmp233-.Ltmp232
	.byte	.Ltmp237-.Ltmp235
	.text

	.text	1
.Ltmp235:
	.inst	0xd500419f

.Ltmp237:
.Ltmp238:
.org (.Ltmp238-(.Ltmp237-.Ltmp235))+(.Ltmp233-.Ltmp232), 0
.Ltmp239:
.org (.Ltmp239-(.Ltmp233-.Ltmp232))+(.Ltmp237-.Ltmp235), 0
	.text


	//NO_APP
	b	.LBB142_18
.Ltmp219:                               // Block address taken
.LBB142_24:
	hint	#36
	//APP
	dsb	sy
	//NO_APP
	b	.LBB142_18
.Ltmp146:                               // Block address taken
.LBB142_25:
	hint	#36
	mov	w11, #160
	b	.LBB142_9
.Ltmp198:                               // Block address taken
.LBB142_26:
	hint	#36
	mov	w9, #160
	b	.LBB142_16
.Lfunc_end142:
	.size	_copy_to_user, .Lfunc_end142-_copy_to_user
                                        // -- End function
	.type	gFtlInitStatus,@object          // @gFtlInitStatus
	.data
	.globl	gFtlInitStatus
	.p2align	2
gFtlInitStatus:
	.word	4294967295                      // 0xffffffff
	.size	gFtlInitStatus, 4

	.type	.L.str,@object                  // @.str
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str:
	.asciz	"\n%s\n"
	.size	.L.str, 5

	.type	.L.str.1,@object                // @.str.1
.L.str.1:
	.asciz	"SFTL version: 5.0.58 20220814"
	.size	.L.str.1, 30

	.type	.L.str.2,@object                // @.str.2
.L.str.2:
	.asciz	"act blk: %x %x %x %x %x %x\n"
	.size	.L.str.2, 28

	.type	g_active_superblock,@object     // @g_active_superblock
	.bss
	.globl	g_active_superblock
	.p2align	2
g_active_superblock:
	.zero	48
	.size	g_active_superblock, 48

	.type	p_valid_page_count_table,@object // @p_valid_page_count_table
	.globl	p_valid_page_count_table
	.p2align	3
p_valid_page_count_table:
	.xword	0
	.size	p_valid_page_count_table, 8

	.type	.L.str.3,@object                // @.str.3
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.3:
	.asciz	"buf blk: %x %x %x %x %x %x\n"
	.size	.L.str.3, 28

	.type	g_buffer_superblock,@object     // @g_buffer_superblock
	.bss
	.globl	g_buffer_superblock
	.p2align	2
g_buffer_superblock:
	.zero	48
	.size	g_buffer_superblock, 48

	.type	.L.str.4,@object                // @.str.4
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.4:
	.asciz	"tmp blk: %x %x %x %x %x %x\n"
	.size	.L.str.4, 28

	.type	g_gc_temp_superblock,@object    // @g_gc_temp_superblock
	.bss
	.globl	g_gc_temp_superblock
	.p2align	2
g_gc_temp_superblock:
	.zero	48
	.size	g_gc_temp_superblock, 48

	.type	.L.str.5,@object                // @.str.5
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.5:
	.asciz	"gc blk: %x %x %x %x %x %x\n"
	.size	.L.str.5, 27

	.type	g_gc_superblock,@object         // @g_gc_superblock
	.bss
	.globl	g_gc_superblock
	.p2align	2
g_gc_superblock:
	.zero	48
	.size	g_gc_superblock, 48

	.type	.L.str.6,@object                // @.str.6
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.6:
	.asciz	"free blk: %x %x %x\n"
	.size	.L.str.6, 20

	.type	g_num_free_superblocks,@object  // @g_num_free_superblocks
	.bss
	.globl	g_num_free_superblocks
	.p2align	2
g_num_free_superblocks:
	.hword	0                               // 0x0
	.size	g_num_free_superblocks, 2

	.type	g_gc_next_blk,@object           // @g_gc_next_blk
	.globl	g_gc_next_blk
	.p2align	2
g_gc_next_blk:
	.hword	0                               // 0x0
	.size	g_gc_next_blk, 2

	.type	g_gc_next_blk_1,@object         // @g_gc_next_blk_1
	.globl	g_gc_next_blk_1
	.p2align	2
g_gc_next_blk_1:
	.hword	0                               // 0x0
	.size	g_gc_next_blk_1, 2

	.type	.L.str.7,@object                // @.str.7
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.7:
	.asciz	"FLASH INFO:\n"
	.size	.L.str.7, 13

	.type	.L.str.8,@object                // @.str.8
.L.str.8:
	.asciz	"Device Capacity: %d MB\n"
	.size	.L.str.8, 24

	.type	DeviceCapacity,@object          // @DeviceCapacity
	.bss
	.globl	DeviceCapacity
	.p2align	2
DeviceCapacity:
	.word	0                               // 0x0
	.size	DeviceCapacity, 4

	.type	.L.str.9,@object                // @.str.9
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.9:
	.asciz	"FTL INFO:\n"
	.size	.L.str.9, 11

	.type	.L.str.10,@object               // @.str.10
.L.str.10:
	.asciz	"g_MaxLpn = 0x%x\n"
	.size	.L.str.10, 17

	.type	g_MaxLpn,@object                // @g_MaxLpn
	.bss
	.globl	g_MaxLpn
	.p2align	2
g_MaxLpn:
	.word	0                               // 0x0
	.size	g_MaxLpn, 4

	.type	.L.str.11,@object               // @.str.11
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.11:
	.asciz	"g_VaildLpn = 0x%x\n"
	.size	.L.str.11, 19

	.type	g_VaildLpn,@object              // @g_VaildLpn
	.bss
	.globl	g_VaildLpn
	.p2align	2
g_VaildLpn:
	.word	0                               // 0x0
	.size	g_VaildLpn, 4

	.type	.L.str.12,@object               // @.str.12
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.12:
	.asciz	"read_page_count = 0x%x\n"
	.size	.L.str.12, 24

	.type	g_totle_read_page_count,@object // @g_totle_read_page_count
	.bss
	.globl	g_totle_read_page_count
	.p2align	2
g_totle_read_page_count:
	.word	0                               // 0x0
	.size	g_totle_read_page_count, 4

	.type	.L.str.13,@object               // @.str.13
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.13:
	.asciz	"discard_page_count = 0x%x\n"
	.size	.L.str.13, 27

	.type	g_totle_discard_page_count,@object // @g_totle_discard_page_count
	.bss
	.globl	g_totle_discard_page_count
	.p2align	2
g_totle_discard_page_count:
	.word	0                               // 0x0
	.size	g_totle_discard_page_count, 4

	.type	.L.str.14,@object               // @.str.14
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.14:
	.asciz	"write_page_count = 0x%x\n"
	.size	.L.str.14, 25

	.type	g_totle_write_page_count,@object // @g_totle_write_page_count
	.bss
	.globl	g_totle_write_page_count
	.p2align	2
g_totle_write_page_count:
	.word	0                               // 0x0
	.size	g_totle_write_page_count, 4

	.type	.L.str.15,@object               // @.str.15
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.15:
	.asciz	"cache_write_count = 0x%x\n"
	.size	.L.str.15, 26

	.type	g_totle_cache_write_count,@object // @g_totle_cache_write_count
	.bss
	.globl	g_totle_cache_write_count
	.p2align	2
g_totle_cache_write_count:
	.word	0                               // 0x0
	.size	g_totle_cache_write_count, 4

	.type	.L.str.16,@object               // @.str.16
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.16:
	.asciz	"l2p_write_count = 0x%x\n"
	.size	.L.str.16, 24

	.type	g_totle_l2p_write_count,@object // @g_totle_l2p_write_count
	.bss
	.globl	g_totle_l2p_write_count
	.p2align	2
g_totle_l2p_write_count:
	.word	0                               // 0x0
	.size	g_totle_l2p_write_count, 4

	.type	.L.str.17,@object               // @.str.17
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.17:
	.asciz	"gc_page_count = 0x%x\n"
	.size	.L.str.17, 22

	.type	g_totle_gc_page_count,@object   // @g_totle_gc_page_count
	.bss
	.globl	g_totle_gc_page_count
	.p2align	2
g_totle_gc_page_count:
	.word	0                               // 0x0
	.size	g_totle_gc_page_count, 4

	.type	.L.str.18,@object               // @.str.18
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.18:
	.asciz	"totle_write = %d MB\n"
	.size	.L.str.18, 21

	.type	g_totle_write_sector,@object    // @g_totle_write_sector
	.bss
	.globl	g_totle_write_sector
	.p2align	2
g_totle_write_sector:
	.word	0                               // 0x0
	.size	g_totle_write_sector, 4

	.type	.L.str.19,@object               // @.str.19
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.19:
	.asciz	"totle_read = %d MB\n"
	.size	.L.str.19, 20

	.type	g_totle_read_sector,@object     // @g_totle_read_sector
	.bss
	.globl	g_totle_read_sector
	.p2align	2
g_totle_read_sector:
	.word	0                               // 0x0
	.size	g_totle_read_sector, 4

	.type	.L.str.20,@object               // @.str.20
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.20:
	.asciz	"GSV = 0x%x\n"
	.size	.L.str.20, 12

	.type	g_GlobalSysVersion,@object      // @g_GlobalSysVersion
	.bss
	.globl	g_GlobalSysVersion
	.p2align	2
g_GlobalSysVersion:
	.word	0                               // 0x0
	.size	g_GlobalSysVersion, 4

	.type	.L.str.21,@object               // @.str.21
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.21:
	.asciz	"GDV = 0x%x\n"
	.size	.L.str.21, 12

	.type	g_GlobalDataVersion,@object     // @g_GlobalDataVersion
	.bss
	.globl	g_GlobalDataVersion
	.p2align	2
g_GlobalDataVersion:
	.word	0                               // 0x0
	.size	g_GlobalDataVersion, 4

	.type	.L.str.22,@object               // @.str.22
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.22:
	.asciz	"bad blk num = %d\n"
	.size	.L.str.22, 18

	.type	gBbtInfo,@object                // @gBbtInfo
	.bss
	.globl	gBbtInfo
	.p2align	3
gBbtInfo:
	.zero	96
	.size	gBbtInfo, 96

	.type	.L.str.23,@object               // @.str.23
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.23:
	.asciz	"free_superblocks = 0x%x\n"
	.size	.L.str.23, 25

	.type	.L.str.24,@object               // @.str.24
.L.str.24:
	.asciz	"mlc_EC = 0x%x\n"
	.size	.L.str.24, 15

	.type	g_totle_mlc_erase_count,@object // @g_totle_mlc_erase_count
	.bss
	.globl	g_totle_mlc_erase_count
	.p2align	2
g_totle_mlc_erase_count:
	.word	0                               // 0x0
	.size	g_totle_mlc_erase_count, 4

	.type	.L.str.25,@object               // @.str.25
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.25:
	.asciz	"slc_EC = 0x%x\n"
	.size	.L.str.25, 15

	.type	g_totle_slc_erase_count,@object // @g_totle_slc_erase_count
	.bss
	.globl	g_totle_slc_erase_count
	.p2align	2
g_totle_slc_erase_count:
	.word	0                               // 0x0
	.size	g_totle_slc_erase_count, 4

	.type	.L.str.26,@object               // @.str.26
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.26:
	.asciz	"avg_EC = 0x%x\n"
	.size	.L.str.26, 15

	.type	g_totle_avg_erase_count,@object // @g_totle_avg_erase_count
	.bss
	.globl	g_totle_avg_erase_count
	.p2align	2
g_totle_avg_erase_count:
	.word	0                               // 0x0
	.size	g_totle_avg_erase_count, 4

	.type	.L.str.27,@object               // @.str.27
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.27:
	.asciz	"sys_EC = 0x%x\n"
	.size	.L.str.27, 15

	.type	g_totle_sys_slc_erase_count,@object // @g_totle_sys_slc_erase_count
	.bss
	.globl	g_totle_sys_slc_erase_count
	.p2align	2
g_totle_sys_slc_erase_count:
	.word	0                               // 0x0
	.size	g_totle_sys_slc_erase_count, 4

	.type	.L.str.28,@object               // @.str.28
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.28:
	.asciz	"max_EC = 0x%x\n"
	.size	.L.str.28, 15

	.type	g_max_erase_count,@object       // @g_max_erase_count
	.bss
	.globl	g_max_erase_count
	.p2align	2
g_max_erase_count:
	.word	0                               // 0x0
	.size	g_max_erase_count, 4

	.type	.L.str.29,@object               // @.str.29
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.29:
	.asciz	"min_EC = 0x%x\n"
	.size	.L.str.29, 15

	.type	g_min_erase_count,@object       // @g_min_erase_count
	.bss
	.globl	g_min_erase_count
	.p2align	2
g_min_erase_count:
	.word	0                               // 0x0
	.size	g_min_erase_count, 4

	.type	.L.str.30,@object               // @.str.30
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.30:
	.asciz	"PLT = 0x%x\n"
	.size	.L.str.30, 12

	.type	g_sys_save_data,@object         // @g_sys_save_data
	.bss
	.globl	g_sys_save_data
	.p2align	2
g_sys_save_data:
	.zero	48
	.size	g_sys_save_data, 48

	.type	.L.str.31,@object               // @.str.31
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.31:
	.asciz	"POT = 0x%x\n"
	.size	.L.str.31, 12

	.type	.L.str.32,@object               // @.str.32
.L.str.32:
	.asciz	"MaxSector = 0x%x\n"
	.size	.L.str.32, 18

	.type	g_MaxLbaSector,@object          // @g_MaxLbaSector
	.bss
	.globl	g_MaxLbaSector
	.p2align	2
g_MaxLbaSector:
	.word	0                               // 0x0
	.size	g_MaxLbaSector, 4

	.type	.L.str.33,@object               // @.str.33
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.33:
	.asciz	"init_sys_blks_pp = 0x%x\n"
	.size	.L.str.33, 25

	.type	c_ftl_nand_init_sys_blks_per_plane,@object // @c_ftl_nand_init_sys_blks_per_plane
	.bss
	.globl	c_ftl_nand_init_sys_blks_per_plane
	.p2align	2
c_ftl_nand_init_sys_blks_per_plane:
	.word	0                               // 0x0
	.size	c_ftl_nand_init_sys_blks_per_plane, 4

	.type	.L.str.34,@object               // @.str.34
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.34:
	.asciz	"sys_blks_pp = 0x%x\n"
	.size	.L.str.34, 20

	.type	c_ftl_nand_sys_blks_per_plane,@object // @c_ftl_nand_sys_blks_per_plane
	.bss
	.globl	c_ftl_nand_sys_blks_per_plane
	.p2align	2
c_ftl_nand_sys_blks_per_plane:
	.word	0                               // 0x0
	.size	c_ftl_nand_sys_blks_per_plane, 4

	.type	.L.str.35,@object               // @.str.35
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.35:
	.asciz	"free sysblock = 0x%x\n"
	.size	.L.str.35, 22

	.type	gSysFreeQueue,@object           // @gSysFreeQueue
	.bss
	.globl	gSysFreeQueue
	.p2align	2
gSysFreeQueue:
	.zero	2056
	.size	gSysFreeQueue, 2056

	.type	.L.str.36,@object               // @.str.36
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.36:
	.asciz	"data_blks_pp = 0x%x\n"
	.size	.L.str.36, 21

	.type	c_ftl_nand_data_blks_per_plane,@object // @c_ftl_nand_data_blks_per_plane
	.bss
	.globl	c_ftl_nand_data_blks_per_plane
	.p2align	2
c_ftl_nand_data_blks_per_plane:
	.hword	0                               // 0x0
	.size	c_ftl_nand_data_blks_per_plane, 2

	.type	.L.str.37,@object               // @.str.37
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.37:
	.asciz	"data_op_blks_pp = 0x%x\n"
	.size	.L.str.37, 24

	.type	c_ftl_nand_data_op_blks_per_plane,@object // @c_ftl_nand_data_op_blks_per_plane
	.bss
	.globl	c_ftl_nand_data_op_blks_per_plane
	.p2align	2
c_ftl_nand_data_op_blks_per_plane:
	.hword	0                               // 0x0
	.size	c_ftl_nand_data_op_blks_per_plane, 2

	.type	.L.str.38,@object               // @.str.38
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.38:
	.asciz	"max_data_blks = 0x%x\n"
	.size	.L.str.38, 22

	.type	c_ftl_nand_max_data_blks,@object // @c_ftl_nand_max_data_blks
	.bss
	.globl	c_ftl_nand_max_data_blks
	.p2align	2
c_ftl_nand_max_data_blks:
	.word	0                               // 0x0
	.size	c_ftl_nand_max_data_blks, 4

	.type	.L.str.39,@object               // @.str.39
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.39:
	.asciz	"Sys.id = 0x%x\n"
	.size	.L.str.39, 15

	.type	gSysInfo,@object                // @gSysInfo
	.bss
	.globl	gSysInfo
	.p2align	2
gSysInfo:
	.zero	16
	.size	gSysInfo, 16

	.type	.L.str.40,@object               // @.str.40
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.40:
	.asciz	"Bbt.id = 0x%x\n"
	.size	.L.str.40, 15

	.type	.L.str.41,@object               // @.str.41
.L.str.41:
	.asciz	"ACT.page = 0x%x\n"
	.size	.L.str.41, 17

	.type	.L.str.42,@object               // @.str.42
.L.str.42:
	.asciz	"ACT.plane = 0x%x\n"
	.size	.L.str.42, 18

	.type	.L.str.43,@object               // @.str.43
.L.str.43:
	.asciz	"ACT.id = 0x%x\n"
	.size	.L.str.43, 15

	.type	.L.str.44,@object               // @.str.44
.L.str.44:
	.asciz	"ACT.mode = 0x%x\n"
	.size	.L.str.44, 17

	.type	.L.str.45,@object               // @.str.45
.L.str.45:
	.asciz	"ACT.a_pages = 0x%x\n"
	.size	.L.str.45, 20

	.type	.L.str.46,@object               // @.str.46
.L.str.46:
	.asciz	"ACT VPC = 0x%x\n"
	.size	.L.str.46, 16

	.type	.L.str.47,@object               // @.str.47
.L.str.47:
	.asciz	"BUF.page = 0x%x\n"
	.size	.L.str.47, 17

	.type	.L.str.48,@object               // @.str.48
.L.str.48:
	.asciz	"BUF.plane = 0x%x\n"
	.size	.L.str.48, 18

	.type	.L.str.49,@object               // @.str.49
.L.str.49:
	.asciz	"BUF.id = 0x%x\n"
	.size	.L.str.49, 15

	.type	.L.str.50,@object               // @.str.50
.L.str.50:
	.asciz	"BUF.mode = 0x%x\n"
	.size	.L.str.50, 17

	.type	.L.str.51,@object               // @.str.51
.L.str.51:
	.asciz	"BUF.a_pages = 0x%x\n"
	.size	.L.str.51, 20

	.type	.L.str.52,@object               // @.str.52
.L.str.52:
	.asciz	"BUF VPC = 0x%x\n"
	.size	.L.str.52, 16

	.type	.L.str.53,@object               // @.str.53
.L.str.53:
	.asciz	"TMP.page = 0x%x\n"
	.size	.L.str.53, 17

	.type	.L.str.54,@object               // @.str.54
.L.str.54:
	.asciz	"TMP.plane = 0x%x\n"
	.size	.L.str.54, 18

	.type	.L.str.55,@object               // @.str.55
.L.str.55:
	.asciz	"TMP.id = 0x%x\n"
	.size	.L.str.55, 15

	.type	.L.str.56,@object               // @.str.56
.L.str.56:
	.asciz	"TMP.mode = 0x%x\n"
	.size	.L.str.56, 17

	.type	.L.str.57,@object               // @.str.57
.L.str.57:
	.asciz	"TMP.a_pages = 0x%x\n"
	.size	.L.str.57, 20

	.type	.L.str.58,@object               // @.str.58
.L.str.58:
	.asciz	"GC.page = 0x%x\n"
	.size	.L.str.58, 16

	.type	.L.str.59,@object               // @.str.59
.L.str.59:
	.asciz	"GC.plane = 0x%x\n"
	.size	.L.str.59, 17

	.type	.L.str.60,@object               // @.str.60
.L.str.60:
	.asciz	"GC.id = 0x%x\n"
	.size	.L.str.60, 14

	.type	.L.str.61,@object               // @.str.61
.L.str.61:
	.asciz	"GC.mode = 0x%x\n"
	.size	.L.str.61, 16

	.type	.L.str.62,@object               // @.str.62
.L.str.62:
	.asciz	"GC.a_pages = 0x%x\n"
	.size	.L.str.62, 19

	.type	.L.str.63,@object               // @.str.63
.L.str.63:
	.asciz	"WR_CHK = %x %x %x\n"
	.size	.L.str.63, 19

	.type	g_sys_ext_data,@object          // @g_sys_ext_data
	.bss
	.globl	g_sys_ext_data
	.p2align	2
g_sys_ext_data:
	.zero	512
	.size	g_sys_ext_data, 512

	.type	.L.str.64,@object               // @.str.64
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.64:
	.asciz	"Read Err Cnt = 0x%x\n"
	.size	.L.str.64, 21

	.type	.L.str.65,@object               // @.str.65
.L.str.65:
	.asciz	"Prog Err Cnt = 0x%x\n"
	.size	.L.str.65, 21

	.type	.L.str.66,@object               // @.str.66
.L.str.66:
	.asciz	"gc_free_blk_th= 0x%x\n"
	.size	.L.str.66, 22

	.type	g_gc_free_blk_threshold,@object // @g_gc_free_blk_threshold
	.bss
	.globl	g_gc_free_blk_threshold
	.p2align	2
g_gc_free_blk_threshold:
	.hword	0                               // 0x0
	.size	g_gc_free_blk_threshold, 2

	.type	.L.str.67,@object               // @.str.67
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.67:
	.asciz	"gc_merge_free_blk_th= 0x%x\n"
	.size	.L.str.67, 28

	.type	g_gc_merge_free_blk_threshold,@object // @g_gc_merge_free_blk_threshold
	.bss
	.globl	g_gc_merge_free_blk_threshold
	.p2align	2
g_gc_merge_free_blk_threshold:
	.hword	0                               // 0x0
	.size	g_gc_merge_free_blk_threshold, 2

	.type	.L.str.68,@object               // @.str.68
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.68:
	.asciz	"gc_skip_write_count= 0x%x\n"
	.size	.L.str.68, 27

	.type	g_gc_skip_write_count,@object   // @g_gc_skip_write_count
	.bss
	.globl	g_gc_skip_write_count
	.p2align	2
g_gc_skip_write_count:
	.word	0                               // 0x0
	.size	g_gc_skip_write_count, 4

	.type	.L.str.69,@object               // @.str.69
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.69:
	.asciz	"gc_blk_index= 0x%x\n"
	.size	.L.str.69, 20

	.type	g_gc_blk_index,@object          // @g_gc_blk_index
	.bss
	.globl	g_gc_blk_index
	.p2align	2
g_gc_blk_index:
	.hword	0                               // 0x0
	.size	g_gc_blk_index, 2

	.type	.L.str.70,@object               // @.str.70
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.70:
	.asciz	"free min EC= 0x%x\n"
	.size	.L.str.70, 19

	.type	.L.str.71,@object               // @.str.71
.L.str.71:
	.asciz	"free max EC= 0x%x\n"
	.size	.L.str.71, 19

	.type	.L.str.72,@object               // @.str.72
.L.str.72:
	.asciz	"GC__SB VPC = 0x%x\n"
	.size	.L.str.72, 19

	.type	.L.str.73,@object               // @.str.73
.L.str.73:
	.asciz	"%d. [0x%x]=0x%x 0x%x  0x%x\n"
	.size	.L.str.73, 28

	.type	p_data_block_list_table,@object // @p_data_block_list_table
	.bss
	.globl	p_data_block_list_table
	.p2align	3
p_data_block_list_table:
	.xword	0
	.size	p_data_block_list_table, 8

	.type	p_erase_count_table,@object     // @p_erase_count_table
	.globl	p_erase_count_table
	.p2align	3
p_erase_count_table:
	.xword	0
	.size	p_erase_count_table, 8

	.type	p_free_data_block_list_head,@object // @p_free_data_block_list_head
	.globl	p_free_data_block_list_head
	.p2align	3
p_free_data_block_list_head:
	.xword	0
	.size	p_free_data_block_list_head, 8

	.type	.L.str.74,@object               // @.str.74
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.74:
	.asciz	"free %d. [0x%x] 0x%x  0x%x\n"
	.size	.L.str.74, 28

	.type	.L.str.75,@object               // @.str.75
.L.str.75:
	.asciz	"%s\n"
	.size	.L.str.75, 4

	.type	c_ftl_nand_planes_num,@object   // @c_ftl_nand_planes_num
	.bss
	.globl	c_ftl_nand_planes_num
	.p2align	2
c_ftl_nand_planes_num:
	.hword	0                               // 0x0
	.size	c_ftl_nand_planes_num, 2

	.type	c_ftl_nand_max_sys_blks,@object // @c_ftl_nand_max_sys_blks
	.globl	c_ftl_nand_max_sys_blks
	.p2align	2
c_ftl_nand_max_sys_blks:
	.word	0                               // 0x0
	.size	c_ftl_nand_max_sys_blks, 4

	.type	c_ftl_nand_blk_pre_plane,@object // @c_ftl_nand_blk_pre_plane
	.globl	c_ftl_nand_blk_pre_plane
	.p2align	2
c_ftl_nand_blk_pre_plane:
	.hword	0                               // 0x0
	.size	c_ftl_nand_blk_pre_plane, 2

	.type	c_ftl_nand_totle_phy_blks,@object // @c_ftl_nand_totle_phy_blks
	.globl	c_ftl_nand_totle_phy_blks
	.p2align	2
c_ftl_nand_totle_phy_blks:
	.word	0                               // 0x0
	.size	c_ftl_nand_totle_phy_blks, 4

	.type	c_ftl_nand_type,@object         // @c_ftl_nand_type
	.globl	c_ftl_nand_type
	.p2align	2
c_ftl_nand_type:
	.hword	0                               // 0x0
	.size	c_ftl_nand_type, 2

	.type	c_ftl_nand_die_num,@object      // @c_ftl_nand_die_num
	.globl	c_ftl_nand_die_num
	.p2align	2
c_ftl_nand_die_num:
	.hword	0                               // 0x0
	.size	c_ftl_nand_die_num, 2

	.type	c_ftl_nand_planes_per_die,@object // @c_ftl_nand_planes_per_die
	.globl	c_ftl_nand_planes_per_die
	.p2align	2
c_ftl_nand_planes_per_die:
	.hword	0                               // 0x0
	.size	c_ftl_nand_planes_per_die, 2

	.type	c_ftl_nand_sec_pre_page,@object // @c_ftl_nand_sec_pre_page
	.globl	c_ftl_nand_sec_pre_page
	.p2align	2
c_ftl_nand_sec_pre_page:
	.hword	0                               // 0x0
	.size	c_ftl_nand_sec_pre_page, 2

	.type	p_plane_order_table,@object     // @p_plane_order_table
	.globl	p_plane_order_table
	.p2align	2
p_plane_order_table:
	.zero	32
	.size	p_plane_order_table, 32

	.type	c_mlc_erase_count_value,@object // @c_mlc_erase_count_value
	.globl	c_mlc_erase_count_value
	.p2align	2
c_mlc_erase_count_value:
	.hword	0                               // 0x0
	.size	c_mlc_erase_count_value, 2

	.type	c_ftl_nand_ext_blk_pre_plane,@object // @c_ftl_nand_ext_blk_pre_plane
	.globl	c_ftl_nand_ext_blk_pre_plane
	.p2align	2
c_ftl_nand_ext_blk_pre_plane:
	.hword	0                               // 0x0
	.size	c_ftl_nand_ext_blk_pre_plane, 2

	.type	c_ftl_vendor_part_size,@object  // @c_ftl_vendor_part_size
	.globl	c_ftl_vendor_part_size
	.p2align	2
c_ftl_vendor_part_size:
	.hword	0                               // 0x0
	.size	c_ftl_vendor_part_size, 2

	.type	c_ftl_nand_blks_per_die,@object // @c_ftl_nand_blks_per_die
	.globl	c_ftl_nand_blks_per_die
	.p2align	2
c_ftl_nand_blks_per_die:
	.hword	0                               // 0x0
	.size	c_ftl_nand_blks_per_die, 2

	.type	c_ftl_nand_blks_per_die_shift,@object // @c_ftl_nand_blks_per_die_shift
	.globl	c_ftl_nand_blks_per_die_shift
	.p2align	2
c_ftl_nand_blks_per_die_shift:
	.hword	0                               // 0x0
	.size	c_ftl_nand_blks_per_die_shift, 2

	.type	c_ftl_nand_page_pre_blk,@object // @c_ftl_nand_page_pre_blk
	.globl	c_ftl_nand_page_pre_blk
	.p2align	2
c_ftl_nand_page_pre_blk:
	.hword	0                               // 0x0
	.size	c_ftl_nand_page_pre_blk, 2

	.type	c_ftl_nand_page_pre_slc_blk,@object // @c_ftl_nand_page_pre_slc_blk
	.globl	c_ftl_nand_page_pre_slc_blk
	.p2align	2
c_ftl_nand_page_pre_slc_blk:
	.hword	0                               // 0x0
	.size	c_ftl_nand_page_pre_slc_blk, 2

	.type	c_ftl_nand_page_pre_super_blk,@object // @c_ftl_nand_page_pre_super_blk
	.globl	c_ftl_nand_page_pre_super_blk
	.p2align	2
c_ftl_nand_page_pre_super_blk:
	.hword	0                               // 0x0
	.size	c_ftl_nand_page_pre_super_blk, 2

	.type	c_ftl_nand_sec_pre_page_shift,@object // @c_ftl_nand_sec_pre_page_shift
	.globl	c_ftl_nand_sec_pre_page_shift
	.p2align	2
c_ftl_nand_sec_pre_page_shift:
	.hword	0                               // 0x0
	.size	c_ftl_nand_sec_pre_page_shift, 2

	.type	c_ftl_nand_byte_pre_page,@object // @c_ftl_nand_byte_pre_page
	.globl	c_ftl_nand_byte_pre_page
	.p2align	2
c_ftl_nand_byte_pre_page:
	.hword	0                               // 0x0
	.size	c_ftl_nand_byte_pre_page, 2

	.type	c_ftl_nand_byte_pre_oob,@object // @c_ftl_nand_byte_pre_oob
	.globl	c_ftl_nand_byte_pre_oob
	.p2align	2
c_ftl_nand_byte_pre_oob:
	.hword	0                               // 0x0
	.size	c_ftl_nand_byte_pre_oob, 2

	.type	c_ftl_nand_reserved_blks,@object // @c_ftl_nand_reserved_blks
	.globl	c_ftl_nand_reserved_blks
	.p2align	2
c_ftl_nand_reserved_blks:
	.hword	0                               // 0x0
	.size	c_ftl_nand_reserved_blks, 2

	.type	c_ftl_nand_max_vendor_blks,@object // @c_ftl_nand_max_vendor_blks
	.globl	c_ftl_nand_max_vendor_blks
	.p2align	2
c_ftl_nand_max_vendor_blks:
	.hword	0                               // 0x0
	.size	c_ftl_nand_max_vendor_blks, 2

	.type	c_ftl_nand_vendor_region_num,@object // @c_ftl_nand_vendor_region_num
	.globl	c_ftl_nand_vendor_region_num
	.p2align	2
c_ftl_nand_vendor_region_num:
	.hword	0                               // 0x0
	.size	c_ftl_nand_vendor_region_num, 2

	.type	c_ftl_nand_map_blks_per_plane,@object // @c_ftl_nand_map_blks_per_plane
	.globl	c_ftl_nand_map_blks_per_plane
	.p2align	2
c_ftl_nand_map_blks_per_plane:
	.hword	0                               // 0x0
	.size	c_ftl_nand_map_blks_per_plane, 2

	.type	c_ftl_nand_max_map_blks,@object // @c_ftl_nand_max_map_blks
	.globl	c_ftl_nand_max_map_blks
	.p2align	2
c_ftl_nand_max_map_blks:
	.word	0                               // 0x0
	.size	c_ftl_nand_max_map_blks, 4

	.type	c_ftl_nand_map_region_num,@object // @c_ftl_nand_map_region_num
	.globl	c_ftl_nand_map_region_num
	.p2align	2
c_ftl_nand_map_region_num:
	.hword	0                               // 0x0
	.size	c_ftl_nand_map_region_num, 2

	.type	c_ftl_nand_l2pmap_ram_region_num,@object // @c_ftl_nand_l2pmap_ram_region_num
	.globl	c_ftl_nand_l2pmap_ram_region_num
	.p2align	2
c_ftl_nand_l2pmap_ram_region_num:
	.hword	0                               // 0x0
	.size	c_ftl_nand_l2pmap_ram_region_num, 2

	.type	g_in_gc_progress,@object        // @g_in_gc_progress
	.globl	g_in_gc_progress
	.p2align	2
g_in_gc_progress:
	.word	0                               // 0x0
	.size	g_in_gc_progress, 4

	.type	g_in_swl_replace,@object        // @g_in_swl_replace
	.globl	g_in_swl_replace
	.p2align	2
g_in_swl_replace:
	.word	0                               // 0x0
	.size	g_in_swl_replace, 4

	.type	g_gc_head_data_block,@object    // @g_gc_head_data_block
	.globl	g_gc_head_data_block
	.p2align	2
g_gc_head_data_block:
	.word	0                               // 0x0
	.size	g_gc_head_data_block, 4

	.type	g_gc_head_data_block_count,@object // @g_gc_head_data_block_count
	.globl	g_gc_head_data_block_count
	.p2align	2
g_gc_head_data_block_count:
	.word	0                               // 0x0
	.size	g_gc_head_data_block_count, 4

	.type	g_cur_erase_blk,@object         // @g_cur_erase_blk
	.globl	g_cur_erase_blk
	.p2align	2
g_cur_erase_blk:
	.word	0                               // 0x0
	.size	g_cur_erase_blk, 4

	.type	g_gc_bad_block_temp_num,@object // @g_gc_bad_block_temp_num
	.globl	g_gc_bad_block_temp_num
	.p2align	2
g_gc_bad_block_temp_num:
	.hword	0                               // 0x0
	.size	g_gc_bad_block_temp_num, 2

	.type	g_gc_bad_block_gc_index,@object // @g_gc_bad_block_gc_index
	.globl	g_gc_bad_block_gc_index
	.p2align	2
g_gc_bad_block_gc_index:
	.hword	0                               // 0x0
	.size	g_gc_bad_block_gc_index, 2

	.type	p_gc_blk_tbl,@object            // @p_gc_blk_tbl
	.globl	p_gc_blk_tbl
	.p2align	3
p_gc_blk_tbl:
	.xword	0
	.size	p_gc_blk_tbl, 8

	.type	p_gc_page_info,@object          // @p_gc_page_info
	.globl	p_gc_page_info
	.p2align	3
p_gc_page_info:
	.xword	0
	.size	p_gc_page_info, 8

	.type	req_read,@object                // @req_read
	.globl	req_read
	.p2align	3
req_read:
	.xword	0
	.size	req_read, 8

	.type	req_gc_dst,@object              // @req_gc_dst
	.globl	req_gc_dst
	.p2align	3
req_gc_dst:
	.xword	0
	.size	req_gc_dst, 8

	.type	req_prgm,@object                // @req_prgm
	.globl	req_prgm
	.p2align	3
req_prgm:
	.xword	0
	.size	req_prgm, 8

	.type	req_erase,@object               // @req_erase
	.globl	req_erase
	.p2align	3
req_erase:
	.xword	0
	.size	req_erase, 8

	.type	req_gc,@object                  // @req_gc
	.globl	req_gc
	.p2align	3
req_gc:
	.xword	0
	.size	req_gc, 8

	.type	c_gc_page_buf_num,@object       // @c_gc_page_buf_num
	.globl	c_gc_page_buf_num
	.p2align	2
c_gc_page_buf_num:
	.word	0                               // 0x0
	.size	c_gc_page_buf_num, 4

	.type	p_sys_data_buf,@object          // @p_sys_data_buf
	.globl	p_sys_data_buf
	.p2align	3
p_sys_data_buf:
	.xword	0
	.size	p_sys_data_buf, 8

	.type	p_sys_data_buf_1,@object        // @p_sys_data_buf_1
	.globl	p_sys_data_buf_1
	.p2align	3
p_sys_data_buf_1:
	.xword	0
	.size	p_sys_data_buf_1, 8

	.type	p_vendor_data_buf,@object       // @p_vendor_data_buf
	.globl	p_vendor_data_buf
	.p2align	3
p_vendor_data_buf:
	.xword	0
	.size	p_vendor_data_buf, 8

	.type	p_gc_data_buf,@object           // @p_gc_data_buf
	.globl	p_gc_data_buf
	.p2align	3
p_gc_data_buf:
	.xword	0
	.size	p_gc_data_buf, 8

	.type	p_io_data_buf_0,@object         // @p_io_data_buf_0
	.globl	p_io_data_buf_0
	.p2align	3
p_io_data_buf_0:
	.xword	0
	.size	p_io_data_buf_0, 8

	.type	p_io_data_buf_1,@object         // @p_io_data_buf_1
	.globl	p_io_data_buf_1
	.p2align	3
p_io_data_buf_1:
	.xword	0
	.size	p_io_data_buf_1, 8

	.type	gp_gc_page_buf_info,@object     // @gp_gc_page_buf_info
	.globl	gp_gc_page_buf_info
	.p2align	3
gp_gc_page_buf_info:
	.xword	0
	.size	gp_gc_page_buf_info, 8

	.type	sftl_nand_check_buf,@object     // @sftl_nand_check_buf
	.globl	sftl_nand_check_buf
	.p2align	3
sftl_nand_check_buf:
	.xword	0
	.size	sftl_nand_check_buf, 8

	.type	sftl_temp_buf,@object           // @sftl_temp_buf
	.globl	sftl_temp_buf
	.p2align	3
sftl_temp_buf:
	.xword	0
	.size	sftl_temp_buf, 8

	.type	sftl_nand_check_spare_buf,@object // @sftl_nand_check_spare_buf
	.globl	sftl_nand_check_spare_buf
	.p2align	3
sftl_nand_check_spare_buf:
	.xword	0
	.size	sftl_nand_check_spare_buf, 8

	.type	p_sys_spare_buf,@object         // @p_sys_spare_buf
	.globl	p_sys_spare_buf
	.p2align	3
p_sys_spare_buf:
	.xword	0
	.size	p_sys_spare_buf, 8

	.type	p_io_spare_buf,@object          // @p_io_spare_buf
	.globl	p_io_spare_buf
	.p2align	3
p_io_spare_buf:
	.xword	0
	.size	p_io_spare_buf, 8

	.type	p_gc_spare_buf,@object          // @p_gc_spare_buf
	.globl	p_gc_spare_buf
	.p2align	3
p_gc_spare_buf:
	.xword	0
	.size	p_gc_spare_buf, 8

	.type	g_ect_tbl_info_size,@object     // @g_ect_tbl_info_size
	.globl	g_ect_tbl_info_size
	.p2align	2
g_ect_tbl_info_size:
	.hword	0                               // 0x0
	.size	g_ect_tbl_info_size, 2

	.type	p_swl_mul_table,@object         // @p_swl_mul_table
	.globl	p_swl_mul_table
	.p2align	3
p_swl_mul_table:
	.xword	0
	.size	p_swl_mul_table, 8

	.type	gp_ect_tbl_info,@object         // @gp_ect_tbl_info
	.globl	gp_ect_tbl_info
	.p2align	3
gp_ect_tbl_info:
	.xword	0
	.size	gp_ect_tbl_info, 8

	.type	p_valid_page_count_check_table,@object // @p_valid_page_count_check_table
	.globl	p_valid_page_count_check_table
	.p2align	3
p_valid_page_count_check_table:
	.xword	0
	.size	p_valid_page_count_check_table, 8

	.type	p_map_block_table,@object       // @p_map_block_table
	.globl	p_map_block_table
	.p2align	3
p_map_block_table:
	.xword	0
	.size	p_map_block_table, 8

	.type	p_map_block_valid_page_count,@object // @p_map_block_valid_page_count
	.globl	p_map_block_valid_page_count
	.p2align	3
p_map_block_valid_page_count:
	.xword	0
	.size	p_map_block_valid_page_count, 8

	.type	p_vendor_block_table,@object    // @p_vendor_block_table
	.globl	p_vendor_block_table
	.p2align	3
p_vendor_block_table:
	.xword	0
	.size	p_vendor_block_table, 8

	.type	p_vendor_block_valid_page_count,@object // @p_vendor_block_valid_page_count
	.globl	p_vendor_block_valid_page_count
	.p2align	3
p_vendor_block_valid_page_count:
	.xword	0
	.size	p_vendor_block_valid_page_count, 8

	.type	p_vendor_block_ver_table,@object // @p_vendor_block_ver_table
	.globl	p_vendor_block_ver_table
	.p2align	3
p_vendor_block_ver_table:
	.xword	0
	.size	p_vendor_block_ver_table, 8

	.type	p_vendor_region_ppn_table,@object // @p_vendor_region_ppn_table
	.globl	p_vendor_region_ppn_table
	.p2align	3
p_vendor_region_ppn_table:
	.xword	0
	.size	p_vendor_region_ppn_table, 8

	.type	p_map_region_ppn_table,@object  // @p_map_region_ppn_table
	.globl	p_map_region_ppn_table
	.p2align	3
p_map_region_ppn_table:
	.xword	0
	.size	p_map_region_ppn_table, 8

	.type	p_map_block_ver_table,@object   // @p_map_block_ver_table
	.globl	p_map_block_ver_table
	.p2align	3
p_map_block_ver_table:
	.xword	0
	.size	p_map_block_ver_table, 8

	.type	p_l2p_ram_map,@object           // @p_l2p_ram_map
	.globl	p_l2p_ram_map
	.p2align	3
p_l2p_ram_map:
	.xword	0
	.size	p_l2p_ram_map, 8

	.type	p_l2p_map_buf,@object           // @p_l2p_map_buf
	.globl	p_l2p_map_buf
	.p2align	3
p_l2p_map_buf:
	.xword	0
	.size	p_l2p_map_buf, 8

	.type	c_ftl_nand_bbm_buf_size,@object // @c_ftl_nand_bbm_buf_size
	.globl	c_ftl_nand_bbm_buf_size
	.p2align	2
c_ftl_nand_bbm_buf_size:
	.hword	0                               // 0x0
	.size	c_ftl_nand_bbm_buf_size, 2

	.type	.L.str.76,@object               // @.str.76
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.76:
	.asciz	"%s error allocating memory. return -1\n"
	.size	.L.str.76, 39

	.type	.L__func__.FtlMemInit,@object   // @__func__.FtlMemInit
.L__func__.FtlMemInit:
	.asciz	"FtlMemInit"
	.size	.L__func__.FtlMemInit, 11

	.type	g_tmp_data_superblock_id,@object // @g_tmp_data_superblock_id
	.bss
	.globl	g_tmp_data_superblock_id
	.p2align	2
g_tmp_data_superblock_id:
	.hword	0                               // 0x0
	.size	g_tmp_data_superblock_id, 2

	.type	g_totle_swl_count,@object       // @g_totle_swl_count
	.globl	g_totle_swl_count
	.p2align	2
g_totle_swl_count:
	.word	0                               // 0x0
	.size	g_totle_swl_count, 4

	.type	ftl_gc_temp_power_lost_recovery_flag,@object // @ftl_gc_temp_power_lost_recovery_flag
	.globl	ftl_gc_temp_power_lost_recovery_flag
	.p2align	2
ftl_gc_temp_power_lost_recovery_flag:
	.word	0                               // 0x0
	.size	ftl_gc_temp_power_lost_recovery_flag, 4

	.type	g_recovery_page_min_ver,@object // @g_recovery_page_min_ver
	.globl	g_recovery_page_min_ver
	.p2align	2
g_recovery_page_min_ver:
	.word	0                               // 0x0
	.size	g_recovery_page_min_ver, 4

	.type	g_totle_vendor_block,@object    // @g_totle_vendor_block
	.globl	g_totle_vendor_block
	.p2align	2
g_totle_vendor_block:
	.hword	0                               // 0x0
	.size	g_totle_vendor_block, 2

	.type	g_MaxLbn,@object                // @g_MaxLbn
	.globl	g_MaxLbn
	.p2align	2
g_MaxLbn:
	.word	0                               // 0x0
	.size	g_MaxLbn, 4

	.type	g_nand_phy_info,@object         // @g_nand_phy_info
	.globl	g_nand_phy_info
	.p2align	1
g_nand_phy_info:
	.zero	24
	.size	g_nand_phy_info, 24

	.type	.L.str.77,@object               // @.str.77
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.77:
	.asciz	"FtlProgPages error %x = %d\n"
	.size	.L.str.77, 28

	.type	.L.str.78,@object               // @.str.78
.L.str.78:
	.asciz	"\n!!!!! error @ func:%s - line:%d\n"
	.size	.L.str.78, 34

	.type	.L__func__.FtlProgPages,@object // @__func__.FtlProgPages
.L__func__.FtlProgPages:
	.asciz	"FtlProgPages"
	.size	.L__func__.FtlProgPages, 13

	.type	.L.str.79,@object               // @.str.79
.L.str.79:
	.asciz	"Ftlwrite decrement_vpc_count %x = %d\n"
	.size	.L.str.79, 38

	.type	power_up_flag,@object           // @power_up_flag
	.data
	.globl	power_up_flag
	.p2align	2
power_up_flag:
	.word	1                               // 0x1
	.size	power_up_flag, 4

	.type	g_ftl_nand_free_count,@object   // @g_ftl_nand_free_count
	.bss
	.globl	g_ftl_nand_free_count
	.p2align	2
g_ftl_nand_free_count:
	.word	0                               // 0x0
	.size	g_ftl_nand_free_count, 4

	.type	.L__func__.FtlWrite,@object     // @__func__.FtlWrite
	.section	.rodata.str1.1,"aMS",@progbits,1
.L__func__.FtlWrite:
	.asciz	"FtlWrite"
	.size	.L__func__.FtlWrite, 9

	.type	.L.str.80,@object               // @.str.80
.L.str.80:
	.asciz	"FtlWrite: ecc error:%x %x %x\n"
	.size	.L.str.80, 30

	.type	.L.str.81,@object               // @.str.81
.L.str.81:
	.asciz	"FtlWrite: lpa error:%x %x\n"
	.size	.L.str.81, 27

	.type	gc_discard_updated,@object      // @gc_discard_updated
	.local	gc_discard_updated
	.comm	gc_discard_updated,4,4
	.type	.L.str.82,@object               // @.str.82
.L.str.82:
	.asciz	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\n"
	.size	.L.str.82, 50

	.type	.L__func__.FtlBbt2Bitmap,@object // @__func__.FtlBbt2Bitmap
.L__func__.FtlBbt2Bitmap:
	.asciz	"FtlBbt2Bitmap"
	.size	.L__func__.FtlBbt2Bitmap, 14

	.type	req_sys,@object                 // @req_sys
	.bss
	.globl	req_sys
	.p2align	3
req_sys:
	.zero	32
	.size	req_sys, 32

	.type	.L.str.83,@object               // @.str.83
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.83:
	.asciz	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\n"
	.size	.L.str.83, 47

	.type	.L.str.84,@object               // @.str.84
.L.str.84:
	.asciz	"FtlBbmTblFlush error:%x\n"
	.size	.L.str.84, 25

	.type	.L.str.85,@object               // @.str.85
.L.str.85:
	.asciz	"FtlBbmTblFlush error = %x error count = %d\n"
	.size	.L.str.85, 44

	.type	.L__func__.FtlLoadBbt,@object   // @__func__.FtlLoadBbt
.L__func__.FtlLoadBbt:
	.asciz	"FtlLoadBbt"
	.size	.L__func__.FtlLoadBbt, 11

	.type	.L.str.86,@object               // @.str.86
.L.str.86:
	.asciz	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\n"
	.size	.L.str.86, 52

	.type	.L__func__.INSERT_FREE_LIST,@object // @__func__.INSERT_FREE_LIST
.L__func__.INSERT_FREE_LIST:
	.asciz	"INSERT_FREE_LIST"
	.size	.L__func__.INSERT_FREE_LIST, 17

	.type	g_num_data_superblocks,@object  // @g_num_data_superblocks
	.bss
	.globl	g_num_data_superblocks
	.p2align	2
g_num_data_superblocks:
	.hword	0                               // 0x0
	.size	g_num_data_superblocks, 2

	.type	.L__func__.INSERT_DATA_LIST,@object // @__func__.INSERT_DATA_LIST
	.section	.rodata.str1.1,"aMS",@progbits,1
.L__func__.INSERT_DATA_LIST:
	.asciz	"INSERT_DATA_LIST"
	.size	.L__func__.INSERT_DATA_LIST, 17

	.type	p_data_block_list_head,@object  // @p_data_block_list_head
	.bss
	.globl	p_data_block_list_head
	.p2align	3
p_data_block_list_head:
	.xword	0
	.size	p_data_block_list_head, 8

	.type	p_data_block_list_tail,@object  // @p_data_block_list_tail
	.globl	p_data_block_list_tail
	.p2align	3
p_data_block_list_tail:
	.xword	0
	.size	p_data_block_list_tail, 8

	.type	.L__func__.List_remove_node,@object // @__func__.List_remove_node
	.section	.rodata.str1.1,"aMS",@progbits,1
.L__func__.List_remove_node:
	.asciz	"List_remove_node"
	.size	.L__func__.List_remove_node, 17

	.type	.L__func__.List_update_data_list,@object // @__func__.List_update_data_list
.L__func__.List_update_data_list:
	.asciz	"List_update_data_list"
	.size	.L__func__.List_update_data_list, 22

	.type	.L__func__.load_l2p_region,@object // @__func__.load_l2p_region
.L__func__.load_l2p_region:
	.asciz	"load_l2p_region"
	.size	.L__func__.load_l2p_region, 16

	.type	.L.str.87,@object               // @.str.87
.L.str.87:
	.asciz	"region_id = %x phyAddr = %x\n"
	.size	.L.str.87, 29

	.type	.L.str.88,@object               // @.str.88
.L.str.88:
	.asciz	"spare:"
	.size	.L.str.88, 7

	.type	.L.str.89,@object               // @.str.89
.L.str.89:
	.asciz	"map_ppn:"
	.size	.L.str.89, 9

	.type	.L.str.90,@object               // @.str.90
.L.str.90:
	.asciz	"load_l2p_region refresh = %x phyAddr = %x\n"
	.size	.L.str.90, 43

	.type	gL2pMapInfo,@object             // @gL2pMapInfo
	.bss
	.globl	gL2pMapInfo
	.p2align	3
gL2pMapInfo:
	.zero	64
	.size	gL2pMapInfo, 64

	.type	.L__func__.ftl_map_blk_alloc_new_blk,@object // @__func__.ftl_map_blk_alloc_new_blk
	.section	.rodata.str1.1,"aMS",@progbits,1
.L__func__.ftl_map_blk_alloc_new_blk:
	.asciz	"ftl_map_blk_alloc_new_blk"
	.size	.L__func__.ftl_map_blk_alloc_new_blk, 26

	.type	.L__func__.ftl_map_blk_gc,@object // @__func__.ftl_map_blk_gc
.L__func__.ftl_map_blk_gc:
	.asciz	"ftl_map_blk_gc"
	.size	.L__func__.ftl_map_blk_gc, 15

	.type	.L__func__.Ftl_write_map_blk_to_last_page,@object // @__func__.Ftl_write_map_blk_to_last_page
.L__func__.Ftl_write_map_blk_to_last_page:
	.asciz	"Ftl_write_map_blk_to_last_page"
	.size	.L__func__.Ftl_write_map_blk_to_last_page, 31

	.type	.L__func__.FtlMapWritePage,@object // @__func__.FtlMapWritePage
.L__func__.FtlMapWritePage:
	.asciz	"FtlMapWritePage"
	.size	.L__func__.FtlMapWritePage, 16

	.type	.L.str.92,@object               // @.str.92
.L.str.92:
	.asciz	"FtlMapWritePage error = %x \n"
	.size	.L.str.92, 29

	.type	.L.str.93,@object               // @.str.93
.L.str.93:
	.asciz	"FtlMapWritePage error = %x error count = %d\n"
	.size	.L.str.93, 45

	.type	g_l2p_last_update_region_id,@object // @g_l2p_last_update_region_id
	.bss
	.globl	g_l2p_last_update_region_id
	.p2align	2
g_l2p_last_update_region_id:
	.hword	0                               // 0x0
	.size	g_l2p_last_update_region_id, 2

	.type	.L__func__.select_l2p_ram_region,@object // @__func__.select_l2p_ram_region
	.section	.rodata.str1.1,"aMS",@progbits,1
.L__func__.select_l2p_ram_region:
	.asciz	"select_l2p_ram_region"
	.size	.L__func__.select_l2p_ram_region, 22

	.type	.L__func__.log2phys,@object     // @__func__.log2phys
.L__func__.log2phys:
	.asciz	"log2phys"
	.size	.L__func__.log2phys, 9

	.type	gVendorBlkInfo,@object          // @gVendorBlkInfo
	.bss
	.globl	gVendorBlkInfo
	.p2align	3
gVendorBlkInfo:
	.zero	64
	.size	gVendorBlkInfo, 64

	.type	.L.str.94,@object               // @.str.94
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.94:
	.asciz	"FtlVendorPartRead refresh = %x phyAddr = %x\n"
	.size	.L.str.94, 45

	.type	g_ect_tbl_power_up_flush,@object // @g_ect_tbl_power_up_flush
	.bss
	.globl	g_ect_tbl_power_up_flush
	.p2align	2
g_ect_tbl_power_up_flush:
	.hword	0                               // 0x0
	.size	g_ect_tbl_power_up_flush, 2

	.type	FtlUpdateVaildLpnCount,@object  // @FtlUpdateVaildLpnCount
	.globl	FtlUpdateVaildLpnCount
	.p2align	2
FtlUpdateVaildLpnCount:
	.hword	0                               // 0x0
	.size	FtlUpdateVaildLpnCount, 2

	.type	.L__func__.FtlVpcTblFlush,@object // @__func__.FtlVpcTblFlush
	.section	.rodata.str1.1,"aMS",@progbits,1
.L__func__.FtlVpcTblFlush:
	.asciz	"FtlVpcTblFlush"
	.size	.L__func__.FtlVpcTblFlush, 15

	.type	.L.str.95,@object               // @.str.95
.L.str.95:
	.asciz	"FtlVpcTblFlush error = %x error count = %d\n"
	.size	.L.str.95, 44

	.type	g_totle_map_block,@object       // @g_totle_map_block
	.bss
	.globl	g_totle_map_block
	.p2align	2
g_totle_map_block:
	.hword	0                               // 0x0
	.size	g_totle_map_block, 2

	.type	.L__func__.FtlScanSysBlk,@object // @__func__.FtlScanSysBlk
	.section	.rodata.str1.1,"aMS",@progbits,1
.L__func__.FtlScanSysBlk:
	.asciz	"FtlScanSysBlk"
	.size	.L__func__.FtlScanSysBlk, 14

	.type	.L.str.96,@object               // @.str.96
.L.str.96:
	.asciz	"no ect"
	.size	.L.str.96, 7

	.type	.L.str.97,@object               // @.str.97
.L.str.97:
	.asciz	"%s hash error this.id =%x page =%x pre_id =%x hash =%x hash_r =%x\n"
	.size	.L.str.97, 67

	.type	.L__func__.FtlLoadSysInfo,@object // @__func__.FtlLoadSysInfo
.L__func__.FtlLoadSysInfo:
	.asciz	"FtlLoadSysInfo"
	.size	.L__func__.FtlLoadSysInfo, 15

	.type	.L.str.98,@object               // @.str.98
.L.str.98:
	.asciz	"%s last blk_id =%x page =%x hash error hash =%x hash_r =%x\n"
	.size	.L.str.98, 60

	.type	.L__func__.FtlMapTblRecovery,@object // @__func__.FtlMapTblRecovery
.L__func__.FtlMapTblRecovery:
	.asciz	"FtlMapTblRecovery"
	.size	.L__func__.FtlMapTblRecovery, 18

	.type	.L.str.99,@object               // @.str.99
.L.str.99:
	.asciz	"%s scan blk_id =%x page =%x hash error hash =%x hash_r =%x\n"
	.size	.L.str.99, 60

	.type	.L__func__.FtlReUsePrevPpa,@object // @__func__.FtlReUsePrevPpa
.L__func__.FtlReUsePrevPpa:
	.asciz	"FtlReUsePrevPpa"
	.size	.L__func__.FtlReUsePrevPpa, 16

	.type	.L__func__.FtlRecoverySuperblock,@object // @__func__.FtlRecoverySuperblock
.L__func__.FtlRecoverySuperblock:
	.asciz	"FtlRecoverySuperblock"
	.size	.L__func__.FtlRecoverySuperblock, 22

	.type	g_power_lost_recovery_flag,@object // @g_power_lost_recovery_flag
	.bss
	.globl	g_power_lost_recovery_flag
	.p2align	2
g_power_lost_recovery_flag:
	.hword	0                               // 0x0
	.size	g_power_lost_recovery_flag, 2

	.type	.L.str.100,@object              // @.str.100
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.100:
	.asciz	"data prev_ppa = %x error...................\n"
	.size	.L.str.100, 45

	.type	.L.str.101,@object              // @.str.101
.L.str.101:
	.asciz	"spuer block %x vpn is 0\n "
	.size	.L.str.101, 26

	.type	g_recovery_page_num,@object     // @g_recovery_page_num
	.bss
	.globl	g_recovery_page_num
	.p2align	2
g_recovery_page_num:
	.word	0                               // 0x0
	.size	g_recovery_page_num, 4

	.type	g_recovery_ppa_tbl,@object      // @g_recovery_ppa_tbl
	.globl	g_recovery_ppa_tbl
	.p2align	2
g_recovery_ppa_tbl:
	.zero	128
	.size	g_recovery_ppa_tbl, 128

	.type	.L__func__.make_superblock,@object // @__func__.make_superblock
	.section	.rodata.str1.1,"aMS",@progbits,1
.L__func__.make_superblock:
	.asciz	"make_superblock"
	.size	.L__func__.make_superblock, 16

	.type	.L__func__.SupperBlkListInit,@object // @__func__.SupperBlkListInit
.L__func__.SupperBlkListInit:
	.asciz	"SupperBlkListInit"
	.size	.L__func__.SupperBlkListInit, 18

	.type	.L.str.102,@object              // @.str.102
.L.str.102:
	.asciz	"...%s enter...\n"
	.size	.L.str.102, 16

	.type	.L__func__.FtlVpcCheckAndModify,@object // @__func__.FtlVpcCheckAndModify
.L__func__.FtlVpcCheckAndModify:
	.asciz	"FtlVpcCheckAndModify"
	.size	.L__func__.FtlVpcCheckAndModify, 21

	.type	.L.str.103,@object              // @.str.103
.L.str.103:
	.asciz	"FtlCheckVpc %x = %x  %x\n"
	.size	.L.str.103, 25

	.type	.L__func__.ftl_check_vpc,@object // @__func__.ftl_check_vpc
.L__func__.ftl_check_vpc:
	.asciz	"ftl_check_vpc"
	.size	.L__func__.ftl_check_vpc, 14

	.type	check_vpc_table,@object         // @check_vpc_table
	.bss
	.globl	check_vpc_table
	.p2align	1
check_vpc_table:
	.zero	16384
	.size	check_vpc_table, 16384

	.type	.L.str.104,@object              // @.str.104
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.104:
	.asciz	"FtlCheckVpc2 %x = %x  %x\n"
	.size	.L.str.104, 26

	.type	.L.str.105,@object              // @.str.105
.L.str.105:
	.asciz	"free blk vpc error %x = %x  %x\n"
	.size	.L.str.105, 32

	.type	.L.str.106,@object              // @.str.106
.L.str.106:
	.asciz	"ftl_scan_all_data = %x\n"
	.size	.L.str.106, 24

	.type	.L.str.107,@object              // @.str.107
.L.str.107:
	.asciz	"scan lpa = %x ppa= %x\n"
	.size	.L.str.107, 23

	.type	.L.str.108,@object              // @.str.108
.L.str.108:
	.asciz	"lpa = %x,addr= %x,spare= %x %x %x %x data=%x %x\n"
	.size	.L.str.108, 49

	.type	.L.str.109,@object              // @.str.109
.L.str.109:
	.asciz	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x\n"
	.size	.L.str.109, 53

	.type	.L__func__.allocate_data_superblock,@object // @__func__.allocate_data_superblock
.L__func__.allocate_data_superblock:
	.asciz	"allocate_data_superblock"
	.size	.L__func__.allocate_data_superblock, 25

	.type	.L__func__.allocate_new_data_superblock,@object // @__func__.allocate_new_data_superblock
.L__func__.allocate_new_data_superblock:
	.asciz	"allocate_new_data_superblock"
	.size	.L__func__.allocate_new_data_superblock, 29

	.type	.L__func__.get_new_active_ppa,@object // @__func__.get_new_active_ppa
.L__func__.get_new_active_ppa:
	.asciz	"get_new_active_ppa"
	.size	.L__func__.get_new_active_ppa, 19

	.type	.L__func__.update_vpc_list,@object // @__func__.update_vpc_list
.L__func__.update_vpc_list:
	.asciz	"update_vpc_list"
	.size	.L__func__.update_vpc_list, 16

	.type	.L.str.110,@object              // @.str.110
.L.str.110:
	.asciz	"decrement_vpc_count %x = %d\n"
	.size	.L.str.110, 29

	.type	.L__func__.decrement_vpc_count,@object // @__func__.decrement_vpc_count
.L__func__.decrement_vpc_count:
	.asciz	"decrement_vpc_count"
	.size	.L__func__.decrement_vpc_count, 20

	.type	g_gc_num_req,@object            // @g_gc_num_req
	.bss
	.globl	g_gc_num_req
	.p2align	2
g_gc_num_req:
	.word	0                               // 0x0
	.size	g_gc_num_req, 4

	.type	g_gc_blk_num,@object            // @g_gc_blk_num
	.globl	g_gc_blk_num
	.p2align	2
g_gc_blk_num:
	.hword	0                               // 0x0
	.size	g_gc_blk_num, 2

	.type	g_gc_page_offset,@object        // @g_gc_page_offset
	.globl	g_gc_page_offset
	.p2align	2
g_gc_page_offset:
	.hword	0                               // 0x0
	.size	g_gc_page_offset, 2

	.type	ftl_gc_temp_block_bops_scan_page_addr,@object // @ftl_gc_temp_block_bops_scan_page_addr
	.data
	.p2align	2
ftl_gc_temp_block_bops_scan_page_addr:
	.hword	65535                           // 0xffff
	.size	ftl_gc_temp_block_bops_scan_page_addr, 2

	.type	.L__func__.FtlGcFreeTempBlock,@object // @__func__.FtlGcFreeTempBlock
	.section	.rodata.str1.1,"aMS",@progbits,1
.L__func__.FtlGcFreeTempBlock:
	.asciz	"FtlGcFreeTempBlock"
	.size	.L__func__.FtlGcFreeTempBlock, 19

	.type	.L.str.111,@object              // @.str.111
.L.str.111:
	.asciz	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \n"
	.size	.L.str.111, 41

	.type	.L.str.112,@object              // @.str.112
.L.str.112:
	.asciz	"FtlGcRefreshBlock  0x%x\n"
	.size	.L.str.112, 25

	.type	.L.str.113,@object              // @.str.113
.L.str.113:
	.asciz	"FtlGcMarkBadPhyBlk %d 0x%x\n"
	.size	.L.str.113, 28

	.type	g_gc_bad_block_temp_tbl,@object // @g_gc_bad_block_temp_tbl
	.bss
	.globl	g_gc_bad_block_temp_tbl
	.p2align	1
g_gc_bad_block_temp_tbl:
	.zero	34
	.size	g_gc_bad_block_temp_tbl, 34

	.type	.L.str.114,@object              // @.str.114
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.114:
	.asciz	"FtlGcFreeBadSuperBlk 0x%x\n"
	.size	.L.str.114, 27

	.type	gc_ink_free_return_value,@object // @gc_ink_free_return_value
	.bss
	.globl	gc_ink_free_return_value
	.p2align	2
gc_ink_free_return_value:
	.hword	0                               // 0x0
	.size	gc_ink_free_return_value, 2

	.type	.L.str.115,@object              // @.str.115
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.115:
	.asciz	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\n"
	.size	.L.str.115, 38

	.type	g_gc_cur_blk_valid_pages,@object // @g_gc_cur_blk_valid_pages
	.bss
	.globl	g_gc_cur_blk_valid_pages
	.p2align	2
g_gc_cur_blk_valid_pages:
	.hword	0                               // 0x0
	.size	g_gc_cur_blk_valid_pages, 2

	.type	g_gc_cur_blk_max_valid_pages,@object // @g_gc_cur_blk_max_valid_pages
	.globl	g_gc_cur_blk_max_valid_pages
	.p2align	2
g_gc_cur_blk_max_valid_pages:
	.hword	0                               // 0x0
	.size	g_gc_cur_blk_max_valid_pages, 2

	.type	.L__func__.rk_ftl_garbage_collect,@object // @__func__.rk_ftl_garbage_collect
	.section	.rodata.str1.1,"aMS",@progbits,1
.L__func__.rk_ftl_garbage_collect:
	.asciz	"rk_ftl_garbage_collect"
	.size	.L__func__.rk_ftl_garbage_collect, 23

	.type	.L.str.116,@object              // @.str.116
.L.str.116:
	.asciz	"%s %p + 0x%x:"
	.size	.L.str.116, 14

	.type	.L.str.117,@object              // @.str.117
.L.str.117:
	.asciz	"0x%08x,"
	.size	.L.str.117, 8

	.type	.L.str.118,@object              // @.str.118
.L.str.118:
	.asciz	"0x%04x,"
	.size	.L.str.118, 8

	.type	.L.str.119,@object              // @.str.119
.L.str.119:
	.asciz	"0x%02x,"
	.size	.L.str.119, 8

	.type	.L.str.120,@object              // @.str.120
.L.str.120:
	.asciz	"\n"
	.size	.L.str.120, 2

	.type	.L__func__.FlashReadPages,@object // @__func__.FlashReadPages
.L__func__.FlashReadPages:
	.asciz	"FlashReadPages"
	.size	.L__func__.FlashReadPages, 15

	.type	g_nand_ops,@object              // @g_nand_ops
	.bss
	.globl	g_nand_ops
	.p2align	3
g_nand_ops:
	.zero	48
	.size	g_nand_ops, 48

	.type	.L.str.121,@object              // @.str.121
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.121:
	.asciz	"FlashReadPages %x %x error_ecc_bits %d\n"
	.size	.L.str.121, 40

	.type	.L.str.122,@object              // @.str.122
.L.str.122:
	.asciz	"data:"
	.size	.L.str.122, 6

	.type	.L__func__.FlashProgPages,@object // @__func__.FlashProgPages
.L__func__.FlashProgPages:
	.asciz	"FlashProgPages"
	.size	.L__func__.FlashProgPages, 15

	.type	.L.str.123,@object              // @.str.123
.L.str.123:
	.asciz	"%s: addr: %x is in id block!!!!!!!!!!\n"
	.size	.L.str.123, 39

	.type	.L.str.124,@object              // @.str.124
.L.str.124:
	.asciz	"not free: w: d:"
	.size	.L.str.124, 16

	.type	.L.str.125,@object              // @.str.125
.L.str.125:
	.asciz	"not free: w: s:"
	.size	.L.str.125, 16

	.type	.L.str.126,@object              // @.str.126
.L.str.126:
	.asciz	"prog read error: = %x\n"
	.size	.L.str.126, 23

	.type	.L.str.127,@object              // @.str.127
.L.str.127:
	.asciz	"prog read REFRESH: = %x\n"
	.size	.L.str.127, 25

	.type	.L.str.128,@object              // @.str.128
.L.str.128:
	.asciz	"prog read s error: = %x %x %x %x %x\n"
	.size	.L.str.128, 37

	.type	.L.str.129,@object              // @.str.129
.L.str.129:
	.asciz	"prog read d error: = %x %x %x %x %x\n"
	.size	.L.str.129, 37

	.type	.L__func__.FlashEraseBlocks,@object // @__func__.FlashEraseBlocks
.L__func__.FlashEraseBlocks:
	.asciz	"FlashEraseBlocks"
	.size	.L__func__.FlashEraseBlocks, 17

	.type	idb_need_write_back,@object     // @idb_need_write_back
	.local	idb_need_write_back
	.comm	idb_need_write_back,1,4
	.type	idb_buf,@object                 // @idb_buf
	.local	idb_buf
	.comm	idb_buf,8,8
	.type	gp_flash_check_buf,@object      // @gp_flash_check_buf
	.local	gp_flash_check_buf
	.comm	gp_flash_check_buf,8,8
	.type	.L.str.130,@object              // @.str.130
.L.str.130:
	.asciz	"%s idb buffer alloc fail\n"
	.size	.L.str.130, 26

	.type	.L__func__.FtlWriteToIDB,@object // @__func__.FtlWriteToIDB
.L__func__.FtlWriteToIDB:
	.asciz	"FtlWriteToIDB"
	.size	.L__func__.FtlWriteToIDB, 14

	.type	_flash_read,@object             // @_flash_read
	.local	_flash_read
	.comm	_flash_read,8,8
	.type	_flash_write,@object            // @_flash_write
	.local	_flash_write
	.comm	_flash_write,8,8
	.type	g_vendor,@object                // @g_vendor
	.local	g_vendor
	.comm	g_vendor,8,8
	.type	.L.str.131,@object              // @.str.131
.L.str.131:
	.asciz	"copy_from_user error %d %p %p\n"
	.size	.L.str.131, 31

	.type	rkflash_vender_storage_dev,@object // @rkflash_vender_storage_dev
	.data
	.p2align	3
rkflash_vender_storage_dev:
	.word	255                             // 0xff
	.zero	4
	.xword	.L.str.135
	.xword	rk_sftl_vendor_storage_fops
	.zero	16
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.hword	0                               // 0x0
	.zero	6
	.size	rkflash_vender_storage_dev, 80

	.type	gp_last_act_superblock,@object  // @gp_last_act_superblock
	.bss
	.globl	gp_last_act_superblock
	.p2align	3
gp_last_act_superblock:
	.xword	0
	.size	gp_last_act_superblock, 8

	.type	.L.str.132,@object              // @.str.132
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str.132:
	.asciz	"write_idblock %x %x\n"
	.size	.L.str.132, 21

	.type	.L.str.133,@object              // @.str.133
.L.str.133:
	.asciz	"write_idblock fail! %x %x %x %x\n"
	.size	.L.str.133, 33

	.type	.L.str.135,@object              // @.str.135
.L.str.135:
	.asciz	"vendor_storage"
	.size	.L.str.135, 15

	.type	rk_sftl_vendor_storage_fops,@object // @rk_sftl_vendor_storage_fops
	.section	.rodata,"a",@progbits
	.p2align	3
rk_sftl_vendor_storage_fops:
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	rk_sftl_vendor_storage_ioctl
	.xword	rk_sftl_vendor_storage_ioctl
	.xword	0
	.xword	0                               // 0x0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.xword	0
	.size	rk_sftl_vendor_storage_fops, 256

	.ident	"Android (7284624, based on r416183b) clang version 12.0.5 (https://android.googlesource.com/toolchain/llvm-project c935d99d7cf2016289302412d708641d52d2f7ee)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
	.addrsig_sym rk_sftl_vendor_storage_ioctl
	.addrsig_sym g_active_superblock
	.addrsig_sym g_buffer_superblock
	.addrsig_sym g_gc_temp_superblock
	.addrsig_sym g_gc_superblock
	.addrsig_sym g_sys_ext_data
	.addrsig_sym p_free_data_block_list_head
	.addrsig_sym g_nand_phy_info
	.addrsig_sym req_sys
	.addrsig_sym gL2pMapInfo
	.addrsig_sym gVendorBlkInfo
	.addrsig_sym rkflash_vender_storage_dev
	.addrsig_sym arm64_const_caps_ready
	.addrsig_sym cpu_hwcap_keys
	.addrsig_sym cpu_hwcaps
	.addrsig_sym gic_nonsecure_priorities
	.addrsig_sym gic_pmr_sync
	.addrsig_sym rk_sftl_vendor_storage_fops
