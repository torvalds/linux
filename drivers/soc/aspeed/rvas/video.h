/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 * video.h
 *
 * This file is part of the ASPEED Linux Device Driver for ASPEED Baseboard Management Controller.
 * Refer to the README file included with this package for driver version and adapter compatibility.
 *
 * Copyright (C) 2019-2021 ASPEED Technology Inc. All rights reserved.
 */

#ifndef __RVAS_VIDEO_H__
#define __RVAS_VIDEO_H__

#define RVAS_DRIVER_NAME "rvas"
#define Stringify(x) #x

//
//functions
//
void ioctl_new_context(struct file *file, struct RvasIoctl *pri, struct AstRVAS *pAstRVAS);
void ioctl_delete_context(struct RvasIoctl *pri, struct AstRVAS *pAstRVAS);
void ioctl_alloc(struct file *file, struct RvasIoctl *pri, struct AstRVAS *pAstRVAS);
void ioctl_free(struct RvasIoctl *pri, struct AstRVAS *pAstRVAS);
void ioctl_update_lms(u8 lms_on, struct AstRVAS *ast_rvas);
u32 ioctl_get_lm_status(struct AstRVAS *ast_rvas);

//void* get_from_rsvd_mem(u32 size, u32 *phys_add, struct AstRVAS *pAstRVAS);
void *get_virt_add_rsvd_mem(u32 index, struct AstRVAS *pAstRVAS);
u32 get_phys_add_rsvd_mem(u32 index, struct AstRVAS *pAstRVAS);
u32 get_len_rsvd_mem(u32 index, struct AstRVAS *pAstRVAS);

//int release_rsvd_mem(u32 size, u32 phys_add);
bool virt_is_valid_rsvd_mem(u32 index, u32 size, struct AstRVAS *pAstRVAS);

struct ContextTable *get_new_context_table_entry(struct AstRVAS *pAstRVAS);
struct ContextTable *get_context_entry(const void *crc, struct AstRVAS *pAstRVAS);
bool remove_context_table_entry(const void *crmh, struct AstRVAS *pAstRVAS);

#endif // __RVAS_VIDEO_H__
