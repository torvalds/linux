/* Copyright 2013-2014 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the above-listed copyright holders nor the
 *       names of any contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*************************************************************************//*
 dprc-cmd.h

 defines dprc portal commands

 *//**************************************************************************/

#ifndef _FSL_DPRC_CMD_H
#define _FSL_DPRC_CMD_H

/* DPRC Version */
#define DPRC_VER_MAJOR				3
#define DPRC_VER_MINOR				0

/* Command IDs */
#define DPRC_CMDID_CLOSE			0x800
#define DPRC_CMDID_OPEN				0x805
#define DPRC_CMDID_CREATE			0x905

#define DPRC_CMDID_GET_ATTR			0x004
#define DPRC_CMDID_RESET_CONT			0x005

#define DPRC_CMDID_SET_IRQ			0x010
#define DPRC_CMDID_GET_IRQ			0x011
#define DPRC_CMDID_SET_IRQ_ENABLE		0x012
#define DPRC_CMDID_GET_IRQ_ENABLE		0x013
#define DPRC_CMDID_SET_IRQ_MASK			0x014
#define DPRC_CMDID_GET_IRQ_MASK			0x015
#define DPRC_CMDID_GET_IRQ_STATUS		0x016
#define DPRC_CMDID_CLEAR_IRQ_STATUS		0x017

#define DPRC_CMDID_CREATE_CONT			0x151
#define DPRC_CMDID_DESTROY_CONT			0x152
#define DPRC_CMDID_SET_RES_QUOTA		0x155
#define DPRC_CMDID_GET_RES_QUOTA		0x156
#define DPRC_CMDID_ASSIGN			0x157
#define DPRC_CMDID_UNASSIGN			0x158
#define DPRC_CMDID_GET_OBJ_COUNT		0x159
#define DPRC_CMDID_GET_OBJ			0x15A
#define DPRC_CMDID_GET_RES_COUNT		0x15B
#define DPRC_CMDID_GET_RES_IDS			0x15C
#define DPRC_CMDID_GET_OBJ_REG			0x15E

#define DPRC_CMDID_CONNECT			0x167
#define DPRC_CMDID_DISCONNECT			0x168
#define DPRC_CMDID_GET_POOL			0x169
#define DPRC_CMDID_GET_POOL_COUNT		0x16A
#define DPRC_CMDID_GET_PORTAL_PADDR		0x16B

#define DPRC_CMDID_GET_CONNECTION		0x16C

#endif /* _FSL_DPRC_CMD_H */
