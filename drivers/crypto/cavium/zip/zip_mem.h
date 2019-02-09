/***********************license start************************************
 * Copyright (c) 2003-2017 Cavium, Inc.
 * All rights reserved.
 *
 * License: one of 'Cavium License' or 'GNU General Public License Version 2'
 *
 * This file is provided under the terms of the Cavium License (see below)
 * or under the terms of GNU General Public License, Version 2, as
 * published by the Free Software Foundation. When using or redistributing
 * this file, you may do so under either license.
 *
 * Cavium License:  Redistribution and use in source and binary forms, with
 * or without modification, are permitted provided that the following
 * conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 *  * Neither the name of Cavium Inc. nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * This Software, including technical data, may be subject to U.S. export
 * control laws, including the U.S. Export Administration Act and its
 * associated regulations, and may be subject to export or import
 * regulations in other countries.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS
 * OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH
 * RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY
 * REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT
 * DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY)
 * WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A
 * PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET
 * ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. THE
 * ENTIRE  RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE LIES
 * WITH YOU.
 ***********************license end**************************************/

#ifndef __ZIP_MEM_H__
#define __ZIP_MEM_H__

/**
 * zip_cmd_qbuf_free - Frees the cmd Queue buffer
 * @zip: Pointer to zip device structure
 * @q:   Queue nmber to free buffer of
 */
void zip_cmd_qbuf_free(struct zip_device *zip, int q);

/**
 * zip_cmd_qbuf_alloc - Allocates a Chunk/cmd buffer for ZIP Inst(cmd) Queue
 * @zip: Pointer to zip device structure
 * @q:   Queue number to allocate bufffer to
 * Return: 0 if successful, 1 otherwise
 */
int zip_cmd_qbuf_alloc(struct zip_device *zip, int q);

/**
 * zip_data_buf_alloc - Allocates memory for a data bufffer
 * @size:   Size of the buffer to allocate
 * Returns: Pointer to the buffer allocated
 */
u8 *zip_data_buf_alloc(u64 size);

/**
 * zip_data_buf_free - Frees the memory of a data buffer
 * @ptr:  Pointer to the buffer
 * @size: Buffer size
 */
void zip_data_buf_free(u8 *ptr, u64 size);

#endif
