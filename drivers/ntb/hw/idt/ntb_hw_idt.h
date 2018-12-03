/*
 *   This file is provided under a GPLv2 license.  When using or
 *   redistributing this file, you may do so under that license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright (C) 2016-2018 T-Platforms JSC All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms and conditions of the GNU General Public License,
 *   version 2, as published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 *   Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, one can be found http://www.gnu.org/licenses/.
 *
 *   The full GNU General Public License is included in this distribution in
 *   the file called "COPYING".
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
 * IDT PCIe-switch NTB Linux driver
 *
 * Contact Information:
 * Serge Semin <fancer.lancer@gmail.com>, <Sergey.Semin@t-platforms.ru>
 */

#ifndef NTB_HW_IDT_H
#define NTB_HW_IDT_H

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/ntb.h>

/*
 * Macro is used to create the struct pci_device_id that matches
 * the supported IDT PCIe-switches
 * @devname: Capitalized name of the particular device
 * @data: Variable passed to the driver of the particular device
 */
#define IDT_PCI_DEVICE_IDS(devname, data) \
	.vendor = PCI_VENDOR_ID_IDT, .device = PCI_DEVICE_ID_IDT_##devname, \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID, \
	.class = (PCI_CLASS_BRIDGE_OTHER << 8), .class_mask = (0xFFFF00), \
	.driver_data = (kernel_ulong_t)&data

/*
 * IDT PCIe-switches device IDs
 */
#define PCI_DEVICE_ID_IDT_89HPES24NT6AG2  0x8091
#define PCI_DEVICE_ID_IDT_89HPES32NT8AG2  0x808F
#define PCI_DEVICE_ID_IDT_89HPES32NT8BG2  0x8088
#define PCI_DEVICE_ID_IDT_89HPES12NT12G2  0x8092
#define PCI_DEVICE_ID_IDT_89HPES16NT16G2  0x8090
#define PCI_DEVICE_ID_IDT_89HPES24NT24G2  0x808E
#define PCI_DEVICE_ID_IDT_89HPES32NT24AG2 0x808C
#define PCI_DEVICE_ID_IDT_89HPES32NT24BG2 0x808A

/*
 * NT-function Configuration Space registers
 * NOTE 1) The IDT PCIe-switch internal data is little-endian
 *      so it must be taken into account in the driver
 *      internals.
 *      2) Additionally the registers should be accessed either
 *      with byte-enables corresponding to their native size or
 *      the size of one DWORD
 *
 * So to simplify the driver code, there is only DWORD-sized read/write
 * operations utilized.
 */
/* PCI Express Configuration Space */
/* PCI Express command/status register	(DWORD) */
#define IDT_NT_PCICMDSTS		0x00004U
/* PCI Express Device Capabilities	(DWORD) */
#define IDT_NT_PCIEDCAP			0x00044U
/* PCI Express Device Control/Status	(WORD+WORD) */
#define IDT_NT_PCIEDCTLSTS		0x00048U
/* PCI Express Link Capabilities	(DWORD) */
#define IDT_NT_PCIELCAP			0x0004CU
/* PCI Express Link Control/Status	(WORD+WORD) */
#define IDT_NT_PCIELCTLSTS		0x00050U
/* PCI Express Device Capabilities 2	(DWORD) */
#define IDT_NT_PCIEDCAP2		0x00064U
/* PCI Express Device Control 2		(WORD+WORD) */
#define IDT_NT_PCIEDCTL2		0x00068U
/* PCI Power Management Control and Status (DWORD) */
#define IDT_NT_PMCSR			0x000C4U
/*==========================================*/
/* IDT Proprietary NT-port-specific registers */
/* NT-function main control registers */
/* NT Endpoint Control			(DWORD) */
#define IDT_NT_NTCTL			0x00400U
/* NT Endpoint Interrupt Status/Mask	(DWORD) */
#define IDT_NT_NTINTSTS			0x00404U
#define IDT_NT_NTINTMSK			0x00408U
/* NT Endpoint Signal Data		(DWORD) */
#define IDT_NT_NTSDATA			0x0040CU
/* NT Endpoint Global Signal		(DWORD) */
#define IDT_NT_NTGSIGNAL		0x00410U
/* Internal Error Reporting Mask 0/1	(DWORD) */
#define IDT_NT_NTIERRORMSK0		0x00414U
#define IDT_NT_NTIERRORMSK1		0x00418U
/* Doorbel registers */
/* NT Outbound Doorbell Set		(DWORD) */
#define IDT_NT_OUTDBELLSET		0x00420U
/* NT Inbound Doorbell Status/Mask	(DWORD) */
#define IDT_NT_INDBELLSTS		0x00428U
#define IDT_NT_INDBELLMSK		0x0042CU
/* Message registers */
/* Outbound Message N			(DWORD) */
#define IDT_NT_OUTMSG0			0x00430U
#define IDT_NT_OUTMSG1			0x00434U
#define IDT_NT_OUTMSG2			0x00438U
#define IDT_NT_OUTMSG3			0x0043CU
/* Inbound Message N			(DWORD) */
#define IDT_NT_INMSG0			0x00440U
#define IDT_NT_INMSG1			0x00444U
#define IDT_NT_INMSG2			0x00448U
#define IDT_NT_INMSG3			0x0044CU
/* Inbound Message Source N		(DWORD) */
#define IDT_NT_INMSGSRC0		0x00450U
#define IDT_NT_INMSGSRC1		0x00454U
#define IDT_NT_INMSGSRC2		0x00458U
#define IDT_NT_INMSGSRC3		0x0045CU
/* Message Status			(DWORD) */
#define IDT_NT_MSGSTS			0x00460U
/* Message Status Mask			(DWORD) */
#define IDT_NT_MSGSTSMSK		0x00464U
/* BAR-setup registers */
/* BAR N Setup/Limit Address/Lower and Upper Translated Base Address (DWORD) */
#define IDT_NT_BARSETUP0		0x00470U
#define IDT_NT_BARLIMIT0		0x00474U
#define IDT_NT_BARLTBASE0		0x00478U
#define IDT_NT_BARUTBASE0		0x0047CU
#define IDT_NT_BARSETUP1		0x00480U
#define IDT_NT_BARLIMIT1		0x00484U
#define IDT_NT_BARLTBASE1		0x00488U
#define IDT_NT_BARUTBASE1		0x0048CU
#define IDT_NT_BARSETUP2		0x00490U
#define IDT_NT_BARLIMIT2		0x00494U
#define IDT_NT_BARLTBASE2		0x00498U
#define IDT_NT_BARUTBASE2		0x0049CU
#define IDT_NT_BARSETUP3		0x004A0U
#define IDT_NT_BARLIMIT3		0x004A4U
#define IDT_NT_BARLTBASE3		0x004A8U
#define IDT_NT_BARUTBASE3		0x004ACU
#define IDT_NT_BARSETUP4		0x004B0U
#define IDT_NT_BARLIMIT4		0x004B4U
#define IDT_NT_BARLTBASE4		0x004B8U
#define IDT_NT_BARUTBASE4		0x004BCU
#define IDT_NT_BARSETUP5		0x004C0U
#define IDT_NT_BARLIMIT5		0x004C4U
#define IDT_NT_BARLTBASE5		0x004C8U
#define IDT_NT_BARUTBASE5		0x004CCU
/* NT mapping table registers */
/* NT Mapping Table Address/Status/Data	(DWORD) */
#define IDT_NT_NTMTBLADDR		0x004D0U
#define IDT_NT_NTMTBLSTS		0x004D4U
#define IDT_NT_NTMTBLDATA		0x004D8U
/* Requester ID (Bus:Device:Function) Capture	(DWORD) */
#define IDT_NT_REQIDCAP			0x004DCU
/* Memory Windows Lookup table registers */
/* Lookup Table Offset/Lower, Middle and Upper data	(DWORD) */
#define IDT_NT_LUTOFFSET		0x004E0U
#define IDT_NT_LUTLDATA			0x004E4U
#define IDT_NT_LUTMDATA			0x004E8U
#define IDT_NT_LUTUDATA			0x004ECU
/* NT Endpoint Uncorrectable/Correctable Errors Emulation registers (DWORD) */
#define IDT_NT_NTUEEM			0x004F0U
#define IDT_NT_NTCEEM			0x004F4U
/* Global Address Space Access/Data registers	(DWARD) */
#define IDT_NT_GASAADDR			0x00FF8U
#define IDT_NT_GASADATA			0x00FFCU

/*
 * IDT PCIe-switch Global Configuration and Status registers
 */
/* Port N Configuration register in global space */
/* PCI Express command/status and link control/status registers (WORD+WORD) */
#define IDT_SW_NTP0_PCIECMDSTS		0x01004U
#define IDT_SW_NTP0_PCIELCTLSTS		0x01050U
/* NT-function control register		(DWORD) */
#define IDT_SW_NTP0_NTCTL		0x01400U
/* BAR setup/limit/base address registers (DWORD) */
#define IDT_SW_NTP0_BARSETUP0		0x01470U
#define IDT_SW_NTP0_BARLIMIT0		0x01474U
#define IDT_SW_NTP0_BARLTBASE0		0x01478U
#define IDT_SW_NTP0_BARUTBASE0		0x0147CU
#define IDT_SW_NTP0_BARSETUP1		0x01480U
#define IDT_SW_NTP0_BARLIMIT1		0x01484U
#define IDT_SW_NTP0_BARLTBASE1		0x01488U
#define IDT_SW_NTP0_BARUTBASE1		0x0148CU
#define IDT_SW_NTP0_BARSETUP2		0x01490U
#define IDT_SW_NTP0_BARLIMIT2		0x01494U
#define IDT_SW_NTP0_BARLTBASE2		0x01498U
#define IDT_SW_NTP0_BARUTBASE2		0x0149CU
#define IDT_SW_NTP0_BARSETUP3		0x014A0U
#define IDT_SW_NTP0_BARLIMIT3		0x014A4U
#define IDT_SW_NTP0_BARLTBASE3		0x014A8U
#define IDT_SW_NTP0_BARUTBASE3		0x014ACU
#define IDT_SW_NTP0_BARSETUP4		0x014B0U
#define IDT_SW_NTP0_BARLIMIT4		0x014B4U
#define IDT_SW_NTP0_BARLTBASE4		0x014B8U
#define IDT_SW_NTP0_BARUTBASE4		0x014BCU
#define IDT_SW_NTP0_BARSETUP5		0x014C0U
#define IDT_SW_NTP0_BARLIMIT5		0x014C4U
#define IDT_SW_NTP0_BARLTBASE5		0x014C8U
#define IDT_SW_NTP0_BARUTBASE5		0x014CCU
/* PCI Express command/status and link control/status registers (WORD+WORD) */
#define IDT_SW_NTP2_PCIECMDSTS		0x05004U
#define IDT_SW_NTP2_PCIELCTLSTS		0x05050U
/* NT-function control register		(DWORD) */
#define IDT_SW_NTP2_NTCTL		0x05400U
/* BAR setup/limit/base address registers (DWORD) */
#define IDT_SW_NTP2_BARSETUP0		0x05470U
#define IDT_SW_NTP2_BARLIMIT0		0x05474U
#define IDT_SW_NTP2_BARLTBASE0		0x05478U
#define IDT_SW_NTP2_BARUTBASE0		0x0547CU
#define IDT_SW_NTP2_BARSETUP1		0x05480U
#define IDT_SW_NTP2_BARLIMIT1		0x05484U
#define IDT_SW_NTP2_BARLTBASE1		0x05488U
#define IDT_SW_NTP2_BARUTBASE1		0x0548CU
#define IDT_SW_NTP2_BARSETUP2		0x05490U
#define IDT_SW_NTP2_BARLIMIT2		0x05494U
#define IDT_SW_NTP2_BARLTBASE2		0x05498U
#define IDT_SW_NTP2_BARUTBASE2		0x0549CU
#define IDT_SW_NTP2_BARSETUP3		0x054A0U
#define IDT_SW_NTP2_BARLIMIT3		0x054A4U
#define IDT_SW_NTP2_BARLTBASE3		0x054A8U
#define IDT_SW_NTP2_BARUTBASE3		0x054ACU
#define IDT_SW_NTP2_BARSETUP4		0x054B0U
#define IDT_SW_NTP2_BARLIMIT4		0x054B4U
#define IDT_SW_NTP2_BARLTBASE4		0x054B8U
#define IDT_SW_NTP2_BARUTBASE4		0x054BCU
#define IDT_SW_NTP2_BARSETUP5		0x054C0U
#define IDT_SW_NTP2_BARLIMIT5		0x054C4U
#define IDT_SW_NTP2_BARLTBASE5		0x054C8U
#define IDT_SW_NTP2_BARUTBASE5		0x054CCU
/* PCI Express command/status and link control/status registers (WORD+WORD) */
#define IDT_SW_NTP4_PCIECMDSTS		0x09004U
#define IDT_SW_NTP4_PCIELCTLSTS		0x09050U
/* NT-function control register		(DWORD) */
#define IDT_SW_NTP4_NTCTL		0x09400U
/* BAR setup/limit/base address registers (DWORD) */
#define IDT_SW_NTP4_BARSETUP0		0x09470U
#define IDT_SW_NTP4_BARLIMIT0		0x09474U
#define IDT_SW_NTP4_BARLTBASE0		0x09478U
#define IDT_SW_NTP4_BARUTBASE0		0x0947CU
#define IDT_SW_NTP4_BARSETUP1		0x09480U
#define IDT_SW_NTP4_BARLIMIT1		0x09484U
#define IDT_SW_NTP4_BARLTBASE1		0x09488U
#define IDT_SW_NTP4_BARUTBASE1		0x0948CU
#define IDT_SW_NTP4_BARSETUP2		0x09490U
#define IDT_SW_NTP4_BARLIMIT2		0x09494U
#define IDT_SW_NTP4_BARLTBASE2		0x09498U
#define IDT_SW_NTP4_BARUTBASE2		0x0949CU
#define IDT_SW_NTP4_BARSETUP3		0x094A0U
#define IDT_SW_NTP4_BARLIMIT3		0x094A4U
#define IDT_SW_NTP4_BARLTBASE3		0x094A8U
#define IDT_SW_NTP4_BARUTBASE3		0x094ACU
#define IDT_SW_NTP4_BARSETUP4		0x094B0U
#define IDT_SW_NTP4_BARLIMIT4		0x094B4U
#define IDT_SW_NTP4_BARLTBASE4		0x094B8U
#define IDT_SW_NTP4_BARUTBASE4		0x094BCU
#define IDT_SW_NTP4_BARSETUP5		0x094C0U
#define IDT_SW_NTP4_BARLIMIT5		0x094C4U
#define IDT_SW_NTP4_BARLTBASE5		0x094C8U
#define IDT_SW_NTP4_BARUTBASE5		0x094CCU
/* PCI Express command/status and link control/status registers (WORD+WORD) */
#define IDT_SW_NTP6_PCIECMDSTS		0x0D004U
#define IDT_SW_NTP6_PCIELCTLSTS		0x0D050U
/* NT-function control register		(DWORD) */
#define IDT_SW_NTP6_NTCTL		0x0D400U
/* BAR setup/limit/base address registers (DWORD) */
#define IDT_SW_NTP6_BARSETUP0		0x0D470U
#define IDT_SW_NTP6_BARLIMIT0		0x0D474U
#define IDT_SW_NTP6_BARLTBASE0		0x0D478U
#define IDT_SW_NTP6_BARUTBASE0		0x0D47CU
#define IDT_SW_NTP6_BARSETUP1		0x0D480U
#define IDT_SW_NTP6_BARLIMIT1		0x0D484U
#define IDT_SW_NTP6_BARLTBASE1		0x0D488U
#define IDT_SW_NTP6_BARUTBASE1		0x0D48CU
#define IDT_SW_NTP6_BARSETUP2		0x0D490U
#define IDT_SW_NTP6_BARLIMIT2		0x0D494U
#define IDT_SW_NTP6_BARLTBASE2		0x0D498U
#define IDT_SW_NTP6_BARUTBASE2		0x0D49CU
#define IDT_SW_NTP6_BARSETUP3		0x0D4A0U
#define IDT_SW_NTP6_BARLIMIT3		0x0D4A4U
#define IDT_SW_NTP6_BARLTBASE3		0x0D4A8U
#define IDT_SW_NTP6_BARUTBASE3		0x0D4ACU
#define IDT_SW_NTP6_BARSETUP4		0x0D4B0U
#define IDT_SW_NTP6_BARLIMIT4		0x0D4B4U
#define IDT_SW_NTP6_BARLTBASE4		0x0D4B8U
#define IDT_SW_NTP6_BARUTBASE4		0x0D4BCU
#define IDT_SW_NTP6_BARSETUP5		0x0D4C0U
#define IDT_SW_NTP6_BARLIMIT5		0x0D4C4U
#define IDT_SW_NTP6_BARLTBASE5		0x0D4C8U
#define IDT_SW_NTP6_BARUTBASE5		0x0D4CCU
/* PCI Express command/status and link control/status registers (WORD+WORD) */
#define IDT_SW_NTP8_PCIECMDSTS		0x11004U
#define IDT_SW_NTP8_PCIELCTLSTS		0x11050U
/* NT-function control register		(DWORD) */
#define IDT_SW_NTP8_NTCTL		0x11400U
/* BAR setup/limit/base address registers (DWORD) */
#define IDT_SW_NTP8_BARSETUP0		0x11470U
#define IDT_SW_NTP8_BARLIMIT0		0x11474U
#define IDT_SW_NTP8_BARLTBASE0		0x11478U
#define IDT_SW_NTP8_BARUTBASE0		0x1147CU
#define IDT_SW_NTP8_BARSETUP1		0x11480U
#define IDT_SW_NTP8_BARLIMIT1		0x11484U
#define IDT_SW_NTP8_BARLTBASE1		0x11488U
#define IDT_SW_NTP8_BARUTBASE1		0x1148CU
#define IDT_SW_NTP8_BARSETUP2		0x11490U
#define IDT_SW_NTP8_BARLIMIT2		0x11494U
#define IDT_SW_NTP8_BARLTBASE2		0x11498U
#define IDT_SW_NTP8_BARUTBASE2		0x1149CU
#define IDT_SW_NTP8_BARSETUP3		0x114A0U
#define IDT_SW_NTP8_BARLIMIT3		0x114A4U
#define IDT_SW_NTP8_BARLTBASE3		0x114A8U
#define IDT_SW_NTP8_BARUTBASE3		0x114ACU
#define IDT_SW_NTP8_BARSETUP4		0x114B0U
#define IDT_SW_NTP8_BARLIMIT4		0x114B4U
#define IDT_SW_NTP8_BARLTBASE4		0x114B8U
#define IDT_SW_NTP8_BARUTBASE4		0x114BCU
#define IDT_SW_NTP8_BARSETUP5		0x114C0U
#define IDT_SW_NTP8_BARLIMIT5		0x114C4U
#define IDT_SW_NTP8_BARLTBASE5		0x114C8U
#define IDT_SW_NTP8_BARUTBASE5		0x114CCU
/* PCI Express command/status and link control/status registers (WORD+WORD) */
#define IDT_SW_NTP12_PCIECMDSTS		0x19004U
#define IDT_SW_NTP12_PCIELCTLSTS	0x19050U
/* NT-function control register		(DWORD) */
#define IDT_SW_NTP12_NTCTL		0x19400U
/* BAR setup/limit/base address registers (DWORD) */
#define IDT_SW_NTP12_BARSETUP0		0x19470U
#define IDT_SW_NTP12_BARLIMIT0		0x19474U
#define IDT_SW_NTP12_BARLTBASE0		0x19478U
#define IDT_SW_NTP12_BARUTBASE0		0x1947CU
#define IDT_SW_NTP12_BARSETUP1		0x19480U
#define IDT_SW_NTP12_BARLIMIT1		0x19484U
#define IDT_SW_NTP12_BARLTBASE1		0x19488U
#define IDT_SW_NTP12_BARUTBASE1		0x1948CU
#define IDT_SW_NTP12_BARSETUP2		0x19490U
#define IDT_SW_NTP12_BARLIMIT2		0x19494U
#define IDT_SW_NTP12_BARLTBASE2		0x19498U
#define IDT_SW_NTP12_BARUTBASE2		0x1949CU
#define IDT_SW_NTP12_BARSETUP3		0x194A0U
#define IDT_SW_NTP12_BARLIMIT3		0x194A4U
#define IDT_SW_NTP12_BARLTBASE3		0x194A8U
#define IDT_SW_NTP12_BARUTBASE3		0x194ACU
#define IDT_SW_NTP12_BARSETUP4		0x194B0U
#define IDT_SW_NTP12_BARLIMIT4		0x194B4U
#define IDT_SW_NTP12_BARLTBASE4		0x194B8U
#define IDT_SW_NTP12_BARUTBASE4		0x194BCU
#define IDT_SW_NTP12_BARSETUP5		0x194C0U
#define IDT_SW_NTP12_BARLIMIT5		0x194C4U
#define IDT_SW_NTP12_BARLTBASE5		0x194C8U
#define IDT_SW_NTP12_BARUTBASE5		0x194CCU
/* PCI Express command/status and link control/status registers (WORD+WORD) */
#define IDT_SW_NTP16_PCIECMDSTS		0x21004U
#define IDT_SW_NTP16_PCIELCTLSTS	0x21050U
/* NT-function control register		(DWORD) */
#define IDT_SW_NTP16_NTCTL		0x21400U
/* BAR setup/limit/base address registers (DWORD) */
#define IDT_SW_NTP16_BARSETUP0		0x21470U
#define IDT_SW_NTP16_BARLIMIT0		0x21474U
#define IDT_SW_NTP16_BARLTBASE0		0x21478U
#define IDT_SW_NTP16_BARUTBASE0		0x2147CU
#define IDT_SW_NTP16_BARSETUP1		0x21480U
#define IDT_SW_NTP16_BARLIMIT1		0x21484U
#define IDT_SW_NTP16_BARLTBASE1		0x21488U
#define IDT_SW_NTP16_BARUTBASE1		0x2148CU
#define IDT_SW_NTP16_BARSETUP2		0x21490U
#define IDT_SW_NTP16_BARLIMIT2		0x21494U
#define IDT_SW_NTP16_BARLTBASE2		0x21498U
#define IDT_SW_NTP16_BARUTBASE2		0x2149CU
#define IDT_SW_NTP16_BARSETUP3		0x214A0U
#define IDT_SW_NTP16_BARLIMIT3		0x214A4U
#define IDT_SW_NTP16_BARLTBASE3		0x214A8U
#define IDT_SW_NTP16_BARUTBASE3		0x214ACU
#define IDT_SW_NTP16_BARSETUP4		0x214B0U
#define IDT_SW_NTP16_BARLIMIT4		0x214B4U
#define IDT_SW_NTP16_BARLTBASE4		0x214B8U
#define IDT_SW_NTP16_BARUTBASE4		0x214BCU
#define IDT_SW_NTP16_BARSETUP5		0x214C0U
#define IDT_SW_NTP16_BARLIMIT5		0x214C4U
#define IDT_SW_NTP16_BARLTBASE5		0x214C8U
#define IDT_SW_NTP16_BARUTBASE5		0x214CCU
/* PCI Express command/status and link control/status registers (WORD+WORD) */
#define IDT_SW_NTP20_PCIECMDSTS		0x29004U
#define IDT_SW_NTP20_PCIELCTLSTS	0x29050U
/* NT-function control register		(DWORD) */
#define IDT_SW_NTP20_NTCTL		0x29400U
/* BAR setup/limit/base address registers (DWORD) */
#define IDT_SW_NTP20_BARSETUP0		0x29470U
#define IDT_SW_NTP20_BARLIMIT0		0x29474U
#define IDT_SW_NTP20_BARLTBASE0		0x29478U
#define IDT_SW_NTP20_BARUTBASE0		0x2947CU
#define IDT_SW_NTP20_BARSETUP1		0x29480U
#define IDT_SW_NTP20_BARLIMIT1		0x29484U
#define IDT_SW_NTP20_BARLTBASE1		0x29488U
#define IDT_SW_NTP20_BARUTBASE1		0x2948CU
#define IDT_SW_NTP20_BARSETUP2		0x29490U
#define IDT_SW_NTP20_BARLIMIT2		0x29494U
#define IDT_SW_NTP20_BARLTBASE2		0x29498U
#define IDT_SW_NTP20_BARUTBASE2		0x2949CU
#define IDT_SW_NTP20_BARSETUP3		0x294A0U
#define IDT_SW_NTP20_BARLIMIT3		0x294A4U
#define IDT_SW_NTP20_BARLTBASE3		0x294A8U
#define IDT_SW_NTP20_BARUTBASE3		0x294ACU
#define IDT_SW_NTP20_BARSETUP4		0x294B0U
#define IDT_SW_NTP20_BARLIMIT4		0x294B4U
#define IDT_SW_NTP20_BARLTBASE4		0x294B8U
#define IDT_SW_NTP20_BARUTBASE4		0x294BCU
#define IDT_SW_NTP20_BARSETUP5		0x294C0U
#define IDT_SW_NTP20_BARLIMIT5		0x294C4U
#define IDT_SW_NTP20_BARLTBASE5		0x294C8U
#define IDT_SW_NTP20_BARUTBASE5		0x294CCU
/* IDT PCIe-switch control register	(DWORD) */
#define IDT_SW_CTL			0x3E000U
/* Boot Configuration Vector Status	(DWORD) */
#define IDT_SW_BCVSTS			0x3E004U
/* Port Clocking Mode			(DWORD) */
#define IDT_SW_PCLKMODE			0x3E008U
/* Reset Drain Delay			(DWORD) */
#define IDT_SW_RDRAINDELAY		0x3E080U
/* Port Operating Mode Change Drain Delay (DWORD) */
#define IDT_SW_POMCDELAY		0x3E084U
/* Side Effect Delay			(DWORD) */
#define IDT_SW_SEDELAY			0x3E088U
/* Upstream Secondary Bus Reset Delay	(DWORD) */
#define IDT_SW_SSBRDELAY		0x3E08CU
/* Switch partition N Control/Status/Failover registers */
#define IDT_SW_SWPART0CTL		0x3E100U
#define IDT_SW_SWPART0STS		0x3E104U
#define IDT_SW_SWPART0FCTL		0x3E108U
#define IDT_SW_SWPART1CTL		0x3E120U
#define IDT_SW_SWPART1STS		0x3E124U
#define IDT_SW_SWPART1FCTL		0x3E128U
#define IDT_SW_SWPART2CTL		0x3E140U
#define IDT_SW_SWPART2STS		0x3E144U
#define IDT_SW_SWPART2FCTL		0x3E148U
#define IDT_SW_SWPART3CTL		0x3E160U
#define IDT_SW_SWPART3STS		0x3E164U
#define IDT_SW_SWPART3FCTL		0x3E168U
#define IDT_SW_SWPART4CTL		0x3E180U
#define IDT_SW_SWPART4STS		0x3E184U
#define IDT_SW_SWPART4FCTL		0x3E188U
#define IDT_SW_SWPART5CTL		0x3E1A0U
#define IDT_SW_SWPART5STS		0x3E1A4U
#define IDT_SW_SWPART5FCTL		0x3E1A8U
#define IDT_SW_SWPART6CTL		0x3E1C0U
#define IDT_SW_SWPART6STS		0x3E1C4U
#define IDT_SW_SWPART6FCTL		0x3E1C8U
#define IDT_SW_SWPART7CTL		0x3E1E0U
#define IDT_SW_SWPART7STS		0x3E1E4U
#define IDT_SW_SWPART7FCTL		0x3E1E8U
/* Switch port N control and status registers */
#define IDT_SW_SWPORT0CTL		0x3E200U
#define IDT_SW_SWPORT0STS		0x3E204U
#define IDT_SW_SWPORT0FCTL		0x3E208U
#define IDT_SW_SWPORT2CTL		0x3E240U
#define IDT_SW_SWPORT2STS		0x3E244U
#define IDT_SW_SWPORT2FCTL		0x3E248U
#define IDT_SW_SWPORT4CTL		0x3E280U
#define IDT_SW_SWPORT4STS		0x3E284U
#define IDT_SW_SWPORT4FCTL		0x3E288U
#define IDT_SW_SWPORT6CTL		0x3E2C0U
#define IDT_SW_SWPORT6STS		0x3E2C4U
#define IDT_SW_SWPORT6FCTL		0x3E2C8U
#define IDT_SW_SWPORT8CTL		0x3E300U
#define IDT_SW_SWPORT8STS		0x3E304U
#define IDT_SW_SWPORT8FCTL		0x3E308U
#define IDT_SW_SWPORT12CTL		0x3E380U
#define IDT_SW_SWPORT12STS		0x3E384U
#define IDT_SW_SWPORT12FCTL		0x3E388U
#define IDT_SW_SWPORT16CTL		0x3E400U
#define IDT_SW_SWPORT16STS		0x3E404U
#define IDT_SW_SWPORT16FCTL		0x3E408U
#define IDT_SW_SWPORT20CTL		0x3E480U
#define IDT_SW_SWPORT20STS		0x3E484U
#define IDT_SW_SWPORT20FCTL		0x3E488U
/* Switch Event registers */
/* Switch Event Status/Mask/Partition mask (DWORD) */
#define IDT_SW_SESTS			0x3EC00U
#define IDT_SW_SEMSK			0x3EC04U
#define IDT_SW_SEPMSK			0x3EC08U
/* Switch Event Link Up/Down Status/Mask (DWORD) */
#define IDT_SW_SELINKUPSTS		0x3EC0CU
#define IDT_SW_SELINKUPMSK		0x3EC10U
#define IDT_SW_SELINKDNSTS		0x3EC14U
#define IDT_SW_SELINKDNMSK		0x3EC18U
/* Switch Event Fundamental Reset Status/Mask (DWORD) */
#define IDT_SW_SEFRSTSTS		0x3EC1CU
#define IDT_SW_SEFRSTMSK		0x3EC20U
/* Switch Event Hot Reset Status/Mask	(DWORD) */
#define IDT_SW_SEHRSTSTS		0x3EC24U
#define IDT_SW_SEHRSTMSK		0x3EC28U
/* Switch Event Failover Mask		(DWORD) */
#define IDT_SW_SEFOVRMSK		0x3EC2CU
/* Switch Event Global Signal Status/Mask (DWORD) */
#define IDT_SW_SEGSIGSTS		0x3EC30U
#define IDT_SW_SEGSIGMSK		0x3EC34U
/* NT Global Doorbell Status		(DWORD) */
#define IDT_SW_GDBELLSTS		0x3EC3CU
/* Switch partition N message M control (msgs routing table) (DWORD) */
#define IDT_SW_SWP0MSGCTL0		0x3EE00U
#define IDT_SW_SWP1MSGCTL0		0x3EE04U
#define IDT_SW_SWP2MSGCTL0		0x3EE08U
#define IDT_SW_SWP3MSGCTL0		0x3EE0CU
#define IDT_SW_SWP4MSGCTL0		0x3EE10U
#define IDT_SW_SWP5MSGCTL0		0x3EE14U
#define IDT_SW_SWP6MSGCTL0		0x3EE18U
#define IDT_SW_SWP7MSGCTL0		0x3EE1CU
#define IDT_SW_SWP0MSGCTL1		0x3EE20U
#define IDT_SW_SWP1MSGCTL1		0x3EE24U
#define IDT_SW_SWP2MSGCTL1		0x3EE28U
#define IDT_SW_SWP3MSGCTL1		0x3EE2CU
#define IDT_SW_SWP4MSGCTL1		0x3EE30U
#define IDT_SW_SWP5MSGCTL1		0x3EE34U
#define IDT_SW_SWP6MSGCTL1		0x3EE38U
#define IDT_SW_SWP7MSGCTL1		0x3EE3CU
#define IDT_SW_SWP0MSGCTL2		0x3EE40U
#define IDT_SW_SWP1MSGCTL2		0x3EE44U
#define IDT_SW_SWP2MSGCTL2		0x3EE48U
#define IDT_SW_SWP3MSGCTL2		0x3EE4CU
#define IDT_SW_SWP4MSGCTL2		0x3EE50U
#define IDT_SW_SWP5MSGCTL2		0x3EE54U
#define IDT_SW_SWP6MSGCTL2		0x3EE58U
#define IDT_SW_SWP7MSGCTL2		0x3EE5CU
#define IDT_SW_SWP0MSGCTL3		0x3EE60U
#define IDT_SW_SWP1MSGCTL3		0x3EE64U
#define IDT_SW_SWP2MSGCTL3		0x3EE68U
#define IDT_SW_SWP3MSGCTL3		0x3EE6CU
#define IDT_SW_SWP4MSGCTL3		0x3EE70U
#define IDT_SW_SWP5MSGCTL3		0x3EE74U
#define IDT_SW_SWP6MSGCTL3		0x3EE78U
#define IDT_SW_SWP7MSGCTL3		0x3EE7CU
/* SMBus Status and Control registers	(DWORD) */
#define IDT_SW_SMBUSSTS			0x3F188U
#define IDT_SW_SMBUSCTL			0x3F18CU
/* Serial EEPROM Interface		(DWORD) */
#define IDT_SW_EEPROMINTF		0x3F190U
/* MBus I/O Expander Address N		(DWORD) */
#define IDT_SW_IOEXPADDR0		0x3F198U
#define IDT_SW_IOEXPADDR1		0x3F19CU
#define IDT_SW_IOEXPADDR2		0x3F1A0U
#define IDT_SW_IOEXPADDR3		0x3F1A4U
#define IDT_SW_IOEXPADDR4		0x3F1A8U
#define IDT_SW_IOEXPADDR5		0x3F1ACU
/* General Purpose Events Control and Status registers (DWORD) */
#define IDT_SW_GPECTL			0x3F1B0U
#define IDT_SW_GPESTS			0x3F1B4U
/* Temperature sensor Control/Status/Alarm/Adjustment/Slope registers */
#define IDT_SW_TMPCTL			0x3F1D4U
#define IDT_SW_TMPSTS			0x3F1D8U
#define IDT_SW_TMPALARM			0x3F1DCU
#define IDT_SW_TMPADJ			0x3F1E0U
#define IDT_SW_TSSLOPE			0x3F1E4U
/* SMBus Configuration Block header log	(DWORD) */
#define IDT_SW_SMBUSCBHL		0x3F1E8U

/*
 * Common registers related constants
 * @IDT_REG_ALIGN:	Registers alignment used in the driver
 * @IDT_REG_PCI_MAX:	Maximum PCI configuration space register value
 * @IDT_REG_SW_MAX:	Maximum global register value
 */
#define IDT_REG_ALIGN			4
#define IDT_REG_PCI_MAX			0x00FFFU
#define IDT_REG_SW_MAX			0x3FFFFU

/*
 * PCICMDSTS register fields related constants
 * @IDT_PCICMDSTS_IOAE:	I/O access enable
 * @IDT_PCICMDSTS_MAE:	Memory access enable
 * @IDT_PCICMDSTS_BME:	Bus master enable
 */
#define IDT_PCICMDSTS_IOAE		0x00000001U
#define IDT_PCICMDSTS_MAE		0x00000002U
#define IDT_PCICMDSTS_BME		0x00000004U

/*
 * PCIEDCAP register fields related constants
 * @IDT_PCIEDCAP_MPAYLOAD_MASK:	 Maximum payload size mask
 * @IDT_PCIEDCAP_MPAYLOAD_FLD:	 Maximum payload size field offset
 * @IDT_PCIEDCAP_MPAYLOAD_S128:	 Max supported payload size of 128 bytes
 * @IDT_PCIEDCAP_MPAYLOAD_S256:	 Max supported payload size of 256 bytes
 * @IDT_PCIEDCAP_MPAYLOAD_S512:	 Max supported payload size of 512 bytes
 * @IDT_PCIEDCAP_MPAYLOAD_S1024: Max supported payload size of 1024 bytes
 * @IDT_PCIEDCAP_MPAYLOAD_S2048: Max supported payload size of 2048 bytes
 */
#define IDT_PCIEDCAP_MPAYLOAD_MASK	0x00000007U
#define IDT_PCIEDCAP_MPAYLOAD_FLD	0
#define IDT_PCIEDCAP_MPAYLOAD_S128	0x00000000U
#define IDT_PCIEDCAP_MPAYLOAD_S256	0x00000001U
#define IDT_PCIEDCAP_MPAYLOAD_S512	0x00000002U
#define IDT_PCIEDCAP_MPAYLOAD_S1024	0x00000003U
#define IDT_PCIEDCAP_MPAYLOAD_S2048	0x00000004U

/*
 * PCIEDCTLSTS registers fields related constants
 * @IDT_PCIEDCTL_MPS_MASK:	Maximum payload size mask
 * @IDT_PCIEDCTL_MPS_FLD:	MPS field offset
 * @IDT_PCIEDCTL_MPS_S128:	Max payload size of 128 bytes
 * @IDT_PCIEDCTL_MPS_S256:	Max payload size of 256 bytes
 * @IDT_PCIEDCTL_MPS_S512:	Max payload size of 512 bytes
 * @IDT_PCIEDCTL_MPS_S1024:	Max payload size of 1024 bytes
 * @IDT_PCIEDCTL_MPS_S2048:	Max payload size of 2048 bytes
 * @IDT_PCIEDCTL_MPS_S4096:	Max payload size of 4096 bytes
 */
#define IDT_PCIEDCTLSTS_MPS_MASK	0x000000E0U
#define IDT_PCIEDCTLSTS_MPS_FLD		5
#define IDT_PCIEDCTLSTS_MPS_S128	0x00000000U
#define IDT_PCIEDCTLSTS_MPS_S256	0x00000020U
#define IDT_PCIEDCTLSTS_MPS_S512	0x00000040U
#define IDT_PCIEDCTLSTS_MPS_S1024	0x00000060U
#define IDT_PCIEDCTLSTS_MPS_S2048	0x00000080U
#define IDT_PCIEDCTLSTS_MPS_S4096	0x000000A0U

/*
 * PCIELCAP register fields related constants
 * @IDT_PCIELCAP_PORTNUM_MASK:	Port number field mask
 * @IDT_PCIELCAP_PORTNUM_FLD:	Port number field offset
 */
#define IDT_PCIELCAP_PORTNUM_MASK	0xFF000000U
#define IDT_PCIELCAP_PORTNUM_FLD	24

/*
 * PCIELCTLSTS registers fields related constants
 * @IDT_PCIELSTS_CLS_MASK:	Current link speed mask
 * @IDT_PCIELSTS_CLS_FLD:	Current link speed field offset
 * @IDT_PCIELSTS_NLW_MASK:	Negotiated link width mask
 * @IDT_PCIELSTS_NLW_FLD:	Negotiated link width field offset
 * @IDT_PCIELSTS_SCLK_COM:	Common slot clock configuration
 */
#define IDT_PCIELCTLSTS_CLS_MASK	0x000F0000U
#define IDT_PCIELCTLSTS_CLS_FLD		16
#define IDT_PCIELCTLSTS_NLW_MASK	0x03F00000U
#define IDT_PCIELCTLSTS_NLW_FLD		20
#define IDT_PCIELCTLSTS_SCLK_COM	0x10000000U

/*
 * NTCTL register fields related constants
 * @IDT_NTCTL_IDPROTDIS:	ID Protection check disable (disable MTBL)
 * @IDT_NTCTL_CPEN:		Completion enable
 * @IDT_NTCTL_RNS:		Request no snoop processing (if MTBL disabled)
 * @IDT_NTCTL_ATP:		Address type processing (if MTBL disabled)
 */
#define IDT_NTCTL_IDPROTDIS		0x00000001U
#define IDT_NTCTL_CPEN			0x00000002U
#define IDT_NTCTL_RNS			0x00000004U
#define IDT_NTCTL_ATP			0x00000008U

/*
 * NTINTSTS register fields related constants
 * @IDT_NTINTSTS_MSG:		Message interrupt bit
 * @IDT_NTINTSTS_DBELL:		Doorbell interrupt bit
 * @IDT_NTINTSTS_SEVENT:	Switch Event interrupt bit
 * @IDT_NTINTSTS_TMPSENSOR:	Temperature sensor interrupt bit
 */
#define IDT_NTINTSTS_MSG		0x00000001U
#define IDT_NTINTSTS_DBELL		0x00000002U
#define IDT_NTINTSTS_SEVENT		0x00000008U
#define IDT_NTINTSTS_TMPSENSOR		0x00000080U

/*
 * NTINTMSK register fields related constants
 * @IDT_NTINTMSK_MSG:		Message interrupt mask bit
 * @IDT_NTINTMSK_DBELL:		Doorbell interrupt mask bit
 * @IDT_NTINTMSK_SEVENT:	Switch Event interrupt mask bit
 * @IDT_NTINTMSK_TMPSENSOR:	Temperature sensor interrupt mask bit
 * @IDT_NTINTMSK_ALL:		NTB-related interrupts mask
 */
#define IDT_NTINTMSK_MSG		0x00000001U
#define IDT_NTINTMSK_DBELL		0x00000002U
#define IDT_NTINTMSK_SEVENT		0x00000008U
#define IDT_NTINTMSK_TMPSENSOR		0x00000080U
#define IDT_NTINTMSK_ALL \
	(IDT_NTINTMSK_MSG | IDT_NTINTMSK_DBELL | IDT_NTINTMSK_SEVENT)

/*
 * NTGSIGNAL register fields related constants
 * @IDT_NTGSIGNAL_SET:	Set global signal of the local partition
 */
#define IDT_NTGSIGNAL_SET		0x00000001U

/*
 * BARSETUP register fields related constants
 * @IDT_BARSETUP_TYPE_MASK:	Mask of the TYPE field
 * @IDT_BARSETUP_TYPE_32:	32-bit addressing BAR
 * @IDT_BARSETUP_TYPE_64:	64-bit addressing BAR
 * @IDT_BARSETUP_PREF:		Value of the BAR prefetchable field
 * @IDT_BARSETUP_SIZE_MASK:	Mask of the SIZE field
 * @IDT_BARSETUP_SIZE_FLD:	SIZE field offset
 * @IDT_BARSETUP_SIZE_CFG:	SIZE field value in case of config space MODE
 * @IDT_BARSETUP_MODE_CFG:	Configuration space BAR mode
 * @IDT_BARSETUP_ATRAN_MASK:	ATRAN field mask
 * @IDT_BARSETUP_ATRAN_FLD:	ATRAN field offset
 * @IDT_BARSETUP_ATRAN_DIR:	Direct address translation memory window
 * @IDT_BARSETUP_ATRAN_LUT12:	12-entry lookup table
 * @IDT_BARSETUP_ATRAN_LUT24:	24-entry lookup table
 * @IDT_BARSETUP_TPART_MASK:	TPART field mask
 * @IDT_BARSETUP_TPART_FLD:	TPART field offset
 * @IDT_BARSETUP_EN:		BAR enable bit
 */
#define IDT_BARSETUP_TYPE_MASK		0x00000006U
#define IDT_BARSETUP_TYPE_FLD		0
#define IDT_BARSETUP_TYPE_32		0x00000000U
#define IDT_BARSETUP_TYPE_64		0x00000004U
#define IDT_BARSETUP_PREF		0x00000008U
#define IDT_BARSETUP_SIZE_MASK		0x000003F0U
#define IDT_BARSETUP_SIZE_FLD		4
#define IDT_BARSETUP_SIZE_CFG		0x000000C0U
#define IDT_BARSETUP_MODE_CFG		0x00000400U
#define IDT_BARSETUP_ATRAN_MASK		0x00001800U
#define IDT_BARSETUP_ATRAN_FLD		11
#define IDT_BARSETUP_ATRAN_DIR		0x00000000U
#define IDT_BARSETUP_ATRAN_LUT12	0x00000800U
#define IDT_BARSETUP_ATRAN_LUT24	0x00001000U
#define IDT_BARSETUP_TPART_MASK		0x0000E000U
#define IDT_BARSETUP_TPART_FLD		13
#define IDT_BARSETUP_EN			0x80000000U

/*
 * NTMTBLDATA register fields related constants
 * @IDT_NTMTBLDATA_VALID:	Set the MTBL entry being valid
 * @IDT_NTMTBLDATA_REQID_MASK:	Bus:Device:Function field mask
 * @IDT_NTMTBLDATA_REQID_FLD:	Bus:Device:Function field offset
 * @IDT_NTMTBLDATA_PART_MASK:	Partition field mask
 * @IDT_NTMTBLDATA_PART_FLD:	Partition field offset
 * @IDT_NTMTBLDATA_ATP_TRANS:	Enable AT field translation on request TLPs
 * @IDT_NTMTBLDATA_CNS_INV:	Enable No Snoop attribute inversion of
 *				Completion TLPs
 * @IDT_NTMTBLDATA_RNS_INV:	Enable No Snoop attribute inversion of
 *				Request TLPs
 */
#define IDT_NTMTBLDATA_VALID		0x00000001U
#define IDT_NTMTBLDATA_REQID_MASK	0x0001FFFEU
#define IDT_NTMTBLDATA_REQID_FLD	1
#define IDT_NTMTBLDATA_PART_MASK	0x000E0000U
#define IDT_NTMTBLDATA_PART_FLD		17
#define IDT_NTMTBLDATA_ATP_TRANS	0x20000000U
#define IDT_NTMTBLDATA_CNS_INV		0x40000000U
#define IDT_NTMTBLDATA_RNS_INV		0x80000000U

/*
 * REQIDCAP register fields related constants
 * @IDT_REQIDCAP_REQID_MASK:	Request ID field mask
 * @IDT_REQIDCAP_REQID_FLD:	Request ID field offset
 */
#define IDT_REQIDCAP_REQID_MASK		0x0000FFFFU
#define IDT_REQIDCAP_REQID_FLD		0

/*
 * LUTOFFSET register fields related constants
 * @IDT_LUTOFFSET_INDEX_MASK:	Lookup table index field mask
 * @IDT_LUTOFFSET_INDEX_FLD:	Lookup table index field offset
 * @IDT_LUTOFFSET_BAR_MASK:	Lookup table BAR select field mask
 * @IDT_LUTOFFSET_BAR_FLD:	Lookup table BAR select field offset
 */
#define IDT_LUTOFFSET_INDEX_MASK	0x0000001FU
#define IDT_LUTOFFSET_INDEX_FLD		0
#define IDT_LUTOFFSET_BAR_MASK		0x00000700U
#define IDT_LUTOFFSET_BAR_FLD		8

/*
 * LUTUDATA register fields related constants
 * @IDT_LUTUDATA_PART_MASK:	Partition field mask
 * @IDT_LUTUDATA_PART_FLD:	Partition field offset
 * @IDT_LUTUDATA_VALID:		Lookup table entry valid bit
 */
#define IDT_LUTUDATA_PART_MASK		0x0000000FU
#define IDT_LUTUDATA_PART_FLD		0
#define IDT_LUTUDATA_VALID		0x80000000U

/*
 * SWPARTxSTS register fields related constants
 * @IDT_SWPARTxSTS_SCI:		Switch partition state change initiated
 * @IDT_SWPARTxSTS_SCC:		Switch partition state change completed
 * @IDT_SWPARTxSTS_STATE_MASK:	Switch partition state mask
 * @IDT_SWPARTxSTS_STATE_FLD:	Switch partition state field offset
 * @IDT_SWPARTxSTS_STATE_DIS:	Switch partition disabled
 * @IDT_SWPARTxSTS_STATE_ACT:	Switch partition enabled
 * @IDT_SWPARTxSTS_STATE_RES:	Switch partition in reset
 * @IDT_SWPARTxSTS_US:		Switch partition has upstream port
 * @IDT_SWPARTxSTS_USID_MASK:	Switch partition upstream port ID mask
 * @IDT_SWPARTxSTS_USID_FLD:	Switch partition upstream port ID field offset
 * @IDT_SWPARTxSTS_NT:		Upstream port has NT function
 * @IDT_SWPARTxSTS_DMA:		Upstream port has DMA function
 */
#define IDT_SWPARTxSTS_SCI		0x00000001U
#define IDT_SWPARTxSTS_SCC		0x00000002U
#define IDT_SWPARTxSTS_STATE_MASK	0x00000060U
#define IDT_SWPARTxSTS_STATE_FLD	5
#define IDT_SWPARTxSTS_STATE_DIS	0x00000000U
#define IDT_SWPARTxSTS_STATE_ACT	0x00000020U
#define IDT_SWPARTxSTS_STATE_RES	0x00000060U
#define IDT_SWPARTxSTS_US		0x00000100U
#define IDT_SWPARTxSTS_USID_MASK	0x00003E00U
#define IDT_SWPARTxSTS_USID_FLD		9
#define IDT_SWPARTxSTS_NT		0x00004000U
#define IDT_SWPARTxSTS_DMA		0x00008000U

/*
 * SWPORTxSTS register fields related constants
 * @IDT_SWPORTxSTS_OMCI:	Operation mode change initiated
 * @IDT_SWPORTxSTS_OMCC:	Operation mode change completed
 * @IDT_SWPORTxSTS_LINKUP:	Link up status
 * @IDT_SWPORTxSTS_DS:		Port lanes behave as downstream lanes
 * @IDT_SWPORTxSTS_MODE_MASK:	Port mode field mask
 * @IDT_SWPORTxSTS_MODE_FLD:	Port mode field offset
 * @IDT_SWPORTxSTS_MODE_DIS:	Port mode - disabled
 * @IDT_SWPORTxSTS_MODE_DS:	Port mode - downstream switch port
 * @IDT_SWPORTxSTS_MODE_US:	Port mode - upstream switch port
 * @IDT_SWPORTxSTS_MODE_NT:	Port mode - NT function
 * @IDT_SWPORTxSTS_MODE_USNT:	Port mode - upstream switch port with NTB
 * @IDT_SWPORTxSTS_MODE_UNAT:	Port mode - unattached
 * @IDT_SWPORTxSTS_MODE_USDMA:	Port mode - upstream switch port with DMA
 * @IDT_SWPORTxSTS_MODE_USNTDMA:Port mode - upstream port with NTB and DMA
 * @IDT_SWPORTxSTS_MODE_NTDMA:	Port mode - NT function with DMA
 * @IDT_SWPORTxSTS_SWPART_MASK:	Port partition field mask
 * @IDT_SWPORTxSTS_SWPART_FLD:	Port partition field offset
 * @IDT_SWPORTxSTS_DEVNUM_MASK:	Port device number field mask
 * @IDT_SWPORTxSTS_DEVNUM_FLD:	Port device number field offset
 */
#define IDT_SWPORTxSTS_OMCI		0x00000001U
#define IDT_SWPORTxSTS_OMCC		0x00000002U
#define IDT_SWPORTxSTS_LINKUP		0x00000010U
#define IDT_SWPORTxSTS_DS		0x00000020U
#define IDT_SWPORTxSTS_MODE_MASK	0x000003C0U
#define IDT_SWPORTxSTS_MODE_FLD		6
#define IDT_SWPORTxSTS_MODE_DIS		0x00000000U
#define IDT_SWPORTxSTS_MODE_DS		0x00000040U
#define IDT_SWPORTxSTS_MODE_US		0x00000080U
#define IDT_SWPORTxSTS_MODE_NT		0x000000C0U
#define IDT_SWPORTxSTS_MODE_USNT	0x00000100U
#define IDT_SWPORTxSTS_MODE_UNAT	0x00000140U
#define IDT_SWPORTxSTS_MODE_USDMA	0x00000180U
#define IDT_SWPORTxSTS_MODE_USNTDMA	0x000001C0U
#define IDT_SWPORTxSTS_MODE_NTDMA	0x00000200U
#define IDT_SWPORTxSTS_SWPART_MASK	0x00001C00U
#define IDT_SWPORTxSTS_SWPART_FLD	10
#define IDT_SWPORTxSTS_DEVNUM_MASK	0x001F0000U
#define IDT_SWPORTxSTS_DEVNUM_FLD	16

/*
 * SEMSK register fields related constants
 * @IDT_SEMSK_LINKUP:	Link Up event mask bit
 * @IDT_SEMSK_LINKDN:	Link Down event mask bit
 * @IDT_SEMSK_GSIGNAL:	Global Signal event mask bit
 */
#define IDT_SEMSK_LINKUP		0x00000001U
#define IDT_SEMSK_LINKDN		0x00000002U
#define IDT_SEMSK_GSIGNAL		0x00000020U

/*
 * SWPxMSGCTL register fields related constants
 * @IDT_SWPxMSGCTL_REG_MASK:	Register select field mask
 * @IDT_SWPxMSGCTL_REG_FLD:	Register select field offset
 * @IDT_SWPxMSGCTL_PART_MASK:	Partition select field mask
 * @IDT_SWPxMSGCTL_PART_FLD:	Partition select field offset
 */
#define IDT_SWPxMSGCTL_REG_MASK		0x00000003U
#define IDT_SWPxMSGCTL_REG_FLD		0
#define IDT_SWPxMSGCTL_PART_MASK	0x00000070U
#define IDT_SWPxMSGCTL_PART_FLD		4

/*
 * TMPCTL register fields related constants
 * @IDT_TMPCTL_LTH_MASK:	Low temperature threshold field mask
 * @IDT_TMPCTL_LTH_FLD:		Low temperature threshold field offset
 * @IDT_TMPCTL_MTH_MASK:	Middle temperature threshold field mask
 * @IDT_TMPCTL_MTH_FLD:		Middle temperature threshold field offset
 * @IDT_TMPCTL_HTH_MASK:	High temperature threshold field mask
 * @IDT_TMPCTL_HTH_FLD:		High temperature threshold field offset
 * @IDT_TMPCTL_PDOWN:		Temperature sensor power down
 */
#define IDT_TMPCTL_LTH_MASK		0x000000FFU
#define IDT_TMPCTL_LTH_FLD		0
#define IDT_TMPCTL_MTH_MASK		0x0000FF00U
#define IDT_TMPCTL_MTH_FLD		8
#define IDT_TMPCTL_HTH_MASK		0x00FF0000U
#define IDT_TMPCTL_HTH_FLD		16
#define IDT_TMPCTL_PDOWN		0x80000000U

/*
 * TMPSTS register fields related constants
 * @IDT_TMPSTS_TEMP_MASK:	Current temperature field mask
 * @IDT_TMPSTS_TEMP_FLD:	Current temperature field offset
 * @IDT_TMPSTS_LTEMP_MASK:	Lowest temperature field mask
 * @IDT_TMPSTS_LTEMP_FLD:	Lowest temperature field offset
 * @IDT_TMPSTS_HTEMP_MASK:	Highest temperature field mask
 * @IDT_TMPSTS_HTEMP_FLD:	Highest temperature field offset
 */
#define IDT_TMPSTS_TEMP_MASK		0x000000FFU
#define IDT_TMPSTS_TEMP_FLD		0
#define IDT_TMPSTS_LTEMP_MASK		0x0000FF00U
#define IDT_TMPSTS_LTEMP_FLD		8
#define IDT_TMPSTS_HTEMP_MASK		0x00FF0000U
#define IDT_TMPSTS_HTEMP_FLD		16

/*
 * TMPALARM register fields related constants
 * @IDT_TMPALARM_LTEMP_MASK:	Lowest temperature field mask
 * @IDT_TMPALARM_LTEMP_FLD:	Lowest temperature field offset
 * @IDT_TMPALARM_HTEMP_MASK:	Highest temperature field mask
 * @IDT_TMPALARM_HTEMP_FLD:	Highest temperature field offset
 * @IDT_TMPALARM_IRQ_MASK:	Alarm IRQ status mask
 */
#define IDT_TMPALARM_LTEMP_MASK		0x0000FF00U
#define IDT_TMPALARM_LTEMP_FLD		8
#define IDT_TMPALARM_HTEMP_MASK		0x00FF0000U
#define IDT_TMPALARM_HTEMP_FLD		16
#define IDT_TMPALARM_IRQ_MASK		0x3F000000U

/*
 * TMPADJ register fields related constants
 * @IDT_TMPADJ_OFFSET_MASK:	Temperature value offset field mask
 * @IDT_TMPADJ_OFFSET_FLD:	Temperature value offset field offset
 */
#define IDT_TMPADJ_OFFSET_MASK		0x000000FFU
#define IDT_TMPADJ_OFFSET_FLD		0

/*
 * Helper macro to get/set the corresponding field value
 * @GET_FIELD:		Retrieve the value of the corresponding field
 * @SET_FIELD:		Set the specified field up
 * @IS_FLD_SET:		Check whether a field is set with value
 */
#define GET_FIELD(field, data) \
	(((u32)(data) & IDT_ ##field## _MASK) >> IDT_ ##field## _FLD)
#define SET_FIELD(field, data, value) \
	(((u32)(data) & ~IDT_ ##field## _MASK) | \
	 ((u32)(value) << IDT_ ##field## _FLD))
#define IS_FLD_SET(field, data, value) \
	(((u32)(data) & IDT_ ##field## _MASK) == IDT_ ##field## _ ##value)

/*
 * Useful registers masks:
 * @IDT_DBELL_MASK:	Doorbell bits mask
 * @IDT_OUTMSG_MASK:	Out messages status bits mask
 * @IDT_INMSG_MASK:	In messages status bits mask
 * @IDT_MSG_MASK:	Any message status bits mask
 */
#define IDT_DBELL_MASK		((u32)0xFFFFFFFFU)
#define IDT_OUTMSG_MASK		((u32)0x0000000FU)
#define IDT_INMSG_MASK		((u32)0x000F0000U)
#define IDT_MSG_MASK		(IDT_INMSG_MASK | IDT_OUTMSG_MASK)

/*
 * Number of IDT NTB resources:
 * @IDT_MSG_CNT:	Number of Message registers
 * @IDT_BAR_CNT:	Number of BARs of each port
 * @IDT_MTBL_ENTRY_CNT:	Number mapping table entries
 */
#define IDT_MSG_CNT		4
#define IDT_BAR_CNT		6
#define IDT_MTBL_ENTRY_CNT	64

/*
 * General IDT PCIe-switch constant
 * @IDT_MAX_NR_PORTS:	Maximum number of ports per IDT PCIe-switch
 * @IDT_MAX_NR_PARTS:	Maximum number of partitions per IDT PCIe-switch
 * @IDT_MAX_NR_PEERS:	Maximum number of NT-peers per IDT PCIe-switch
 * @IDT_MAX_NR_MWS:	Maximum number of Memory Widows
 * @IDT_PCIE_REGSIZE:	Size of the registers in bytes
 * @IDT_TRANS_ALIGN:	Alignment of translated base address
 * @IDT_DIR_SIZE_ALIGN:	Alignment of size setting for direct translated MWs.
 *			Even though the lower 10 bits are reserved, they are
 *			treated by IDT as one's so basically there is no any
 *			alignment of size limit for DIR address translation.
 */
#define IDT_MAX_NR_PORTS	24
#define IDT_MAX_NR_PARTS	8
#define IDT_MAX_NR_PEERS	8
#define IDT_MAX_NR_MWS		29
#define IDT_PCIE_REGSIZE	4
#define IDT_TRANS_ALIGN		4
#define IDT_DIR_SIZE_ALIGN	1

/*
 * IDT PCIe-switch temperature sensor value limits
 * @IDT_TEMP_MIN_MDEG:	Minimal integer value of temperature
 * @IDT_TEMP_MAX_MDEG:	Maximal integer value of temperature
 * @IDT_TEMP_MIN_OFFSET:Minimal integer value of temperature offset
 * @IDT_TEMP_MAX_OFFSET:Maximal integer value of temperature offset
 */
#define IDT_TEMP_MIN_MDEG	0
#define IDT_TEMP_MAX_MDEG	127500
#define IDT_TEMP_MIN_OFFSET	-64000
#define IDT_TEMP_MAX_OFFSET	63500

/*
 * Temperature sensor values enumeration
 * @IDT_TEMP_CUR:	Current temperature
 * @IDT_TEMP_LOW:	Lowest historical temperature
 * @IDT_TEMP_HIGH:	Highest historical temperature
 * @IDT_TEMP_OFFSET:	Current temperature offset
 */
enum idt_temp_val {
	IDT_TEMP_CUR,
	IDT_TEMP_LOW,
	IDT_TEMP_HIGH,
	IDT_TEMP_OFFSET
};

/*
 * IDT Memory Windows type. Depending on the device settings, IDT supports
 * Direct Address Translation MW registers and Lookup Table registers
 * @IDT_MW_DIR:		Direct address translation
 * @IDT_MW_LUT12:	12-entry lookup table entry
 * @IDT_MW_LUT24:	24-entry lookup table entry
 *
 * NOTE These values are exactly the same as one of the BARSETUP ATRAN field
 */
enum idt_mw_type {
	IDT_MW_DIR = 0x0,
	IDT_MW_LUT12 = 0x1,
	IDT_MW_LUT24 = 0x2
};

/*
 * IDT PCIe-switch model private data
 * @name:	Device name
 * @port_cnt:	Total number of NT endpoint ports
 * @ports:	Port ids
 */
struct idt_89hpes_cfg {
	char *name;
	unsigned char port_cnt;
	unsigned char ports[];
};

/*
 * Memory window configuration structure
 * @type:	Type of the memory window (direct address translation or lookup
 *		table)
 *
 * @bar:	PCIe BAR the memory window referenced to
 * @idx:	Index of the memory window within the BAR
 *
 * @addr_align:	Alignment of translated address
 * @size_align:	Alignment of memory window size
 * @size_max:	Maximum size of memory window
 */
struct idt_mw_cfg {
	enum idt_mw_type type;

	unsigned char bar;
	unsigned char idx;

	u64 addr_align;
	u64 size_align;
	u64 size_max;
};

/*
 * Description structure of peer IDT NT-functions:
 * @port:		NT-function port
 * @part:		NT-function partition
 *
 * @mw_cnt:		Number of memory windows supported by NT-function
 * @mws:		Array of memory windows descriptors
 */
struct idt_ntb_peer {
	unsigned char port;
	unsigned char part;

	unsigned char mw_cnt;
	struct idt_mw_cfg *mws;
};

/*
 * Description structure of local IDT NT-function:
 * @ntb:		Linux NTB-device description structure
 * @swcfg:		Pointer to the structure of local IDT PCIe-switch
 *			specific cofnfigurations
 *
 * @port:		Local NT-function port
 * @part:		Local NT-function partition
 *
 * @peer_cnt:		Number of peers with activated NTB-function
 * @peers:		Array of peers descripting structures
 * @port_idx_map:	Map of port number -> peer index
 * @part_idx_map:	Map of partition number -> peer index
 *
 * @mtbl_lock:		Mapping table access lock
 *
 * @mw_cnt:		Number of memory windows supported by NT-function
 * @mws:		Array of memory windows descriptors
 * @lut_lock:		Lookup table access lock
 *
 * @msg_locks:		Message registers mapping table lockers
 *
 * @cfgspc:		Virtual address of the memory mapped configuration
 *			space of the NT-function
 * @db_mask_lock:	Doorbell mask register lock
 * @msg_mask_lock:	Message mask register lock
 * @gasa_lock:		GASA registers access lock
 *
 * @hwmon_mtx:		Temperature sensor interface update mutex
 *
 * @dbgfs_info:		DebugFS info node
 */
struct idt_ntb_dev {
	struct ntb_dev ntb;
	struct idt_89hpes_cfg *swcfg;

	unsigned char port;
	unsigned char part;

	unsigned char peer_cnt;
	struct idt_ntb_peer peers[IDT_MAX_NR_PEERS];
	char port_idx_map[IDT_MAX_NR_PORTS];
	char part_idx_map[IDT_MAX_NR_PARTS];

	spinlock_t mtbl_lock;

	unsigned char mw_cnt;
	struct idt_mw_cfg *mws;
	spinlock_t lut_lock;

	spinlock_t msg_locks[IDT_MSG_CNT];

	void __iomem *cfgspc;
	spinlock_t db_mask_lock;
	spinlock_t msg_mask_lock;
	spinlock_t gasa_lock;

	struct mutex hwmon_mtx;

	struct dentry *dbgfs_info;
};
#define to_ndev_ntb(__ntb) container_of(__ntb, struct idt_ntb_dev, ntb)

/*
 * Descriptor of the IDT PCIe-switch BAR resources
 * @setup:	BAR setup register
 * @limit:	BAR limit register
 * @ltbase:	Lower translated base address
 * @utbase:	Upper translated base address
 */
struct idt_ntb_bar {
	unsigned int setup;
	unsigned int limit;
	unsigned int ltbase;
	unsigned int utbase;
};

/*
 * Descriptor of the IDT PCIe-switch message resources
 * @in:		Inbound message register
 * @out:	Outbound message register
 * @src:	Source of inbound message register
 */
struct idt_ntb_msg {
	unsigned int in;
	unsigned int out;
	unsigned int src;
};

/*
 * Descriptor of the IDT PCIe-switch NT-function specific parameters in the
 * PCI Configuration Space
 * @bars:	BARs related registers
 * @msgs:	Messaging related registers
 */
struct idt_ntb_regs {
	struct idt_ntb_bar bars[IDT_BAR_CNT];
	struct idt_ntb_msg msgs[IDT_MSG_CNT];
};

/*
 * Descriptor of the IDT PCIe-switch port specific parameters in the
 * Global Configuration Space
 * @pcicmdsts:	 PCI command/status register
 * @pcielctlsts: PCIe link control/status
 *
 * @ctl:	Port control register
 * @sts:	Port status register
 *
 * @bars:	BARs related registers
 */
struct idt_ntb_port {
	unsigned int pcicmdsts;
	unsigned int pcielctlsts;
	unsigned int ntctl;

	unsigned int ctl;
	unsigned int sts;

	struct idt_ntb_bar bars[IDT_BAR_CNT];
};

/*
 * Descriptor of the IDT PCIe-switch partition specific parameters.
 * @ctl:	Partition control register in the Global Address Space
 * @sts:	Partition status register in the Global Address Space
 * @msgctl:	Messages control registers
 */
struct idt_ntb_part {
	unsigned int ctl;
	unsigned int sts;
	unsigned int msgctl[IDT_MSG_CNT];
};

#endif /* NTB_HW_IDT_H */
