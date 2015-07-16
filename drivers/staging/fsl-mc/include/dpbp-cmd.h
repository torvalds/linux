/* Copyright 2013-2014 Freescale Semiconductor Inc.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
* * Redistributions of source code must retain the above copyright
* notice, this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of the above-listed copyright holders nor the
* names of any contributors may be used to endorse or promote products
* derived from this software without specific prior written permission.
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
#ifndef _FSL_DPBP_CMD_H
#define _FSL_DPBP_CMD_H

/* DPBP Version */
#define DPBP_VER_MAJOR				2
#define DPBP_VER_MINOR				0

/* Command IDs */
#define DPBP_CMDID_CLOSE				0x800
#define DPBP_CMDID_OPEN					0x804
#define DPBP_CMDID_CREATE				0x904
#define DPBP_CMDID_DESTROY				0x900

#define DPBP_CMDID_ENABLE				0x002
#define DPBP_CMDID_DISABLE				0x003
#define DPBP_CMDID_GET_ATTR				0x004
#define DPBP_CMDID_RESET				0x005
#define DPBP_CMDID_IS_ENABLED				0x006

#define DPBP_CMDID_SET_IRQ				0x010
#define DPBP_CMDID_GET_IRQ				0x011
#define DPBP_CMDID_SET_IRQ_ENABLE			0x012
#define DPBP_CMDID_GET_IRQ_ENABLE			0x013
#define DPBP_CMDID_SET_IRQ_MASK				0x014
#define DPBP_CMDID_GET_IRQ_MASK				0x015
#define DPBP_CMDID_GET_IRQ_STATUS			0x016
#define DPBP_CMDID_CLEAR_IRQ_STATUS			0x017

#endif /* _FSL_DPBP_CMD_H */
