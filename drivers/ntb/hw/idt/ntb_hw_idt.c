/*
 *   This file is provided under a GPLv2 license.  When using or
 *   redistributing this file, you may do so under that license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright (C) 2016 T-Platforms All Rights Reserved.
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

#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/sizes.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/ntb.h>

#include "ntb_hw_idt.h"

#define NTB_NAME	"ntb_hw_idt"
#define NTB_DESC	"IDT PCI-E Non-Transparent Bridge Driver"
#define NTB_VER		"2.0"
#define NTB_IRQNAME	"ntb_irq_idt"

MODULE_DESCRIPTION(NTB_DESC);
MODULE_VERSION(NTB_VER);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("T-platforms");

/*
 * NT Endpoint registers table simplifying a loop access to the functionally
 * related registers
 */
static const struct idt_ntb_regs ntdata_tbl = {
	{ {IDT_NT_BARSETUP0,	IDT_NT_BARLIMIT0,
	   IDT_NT_BARLTBASE0,	IDT_NT_BARUTBASE0},
	  {IDT_NT_BARSETUP1,	IDT_NT_BARLIMIT1,
	   IDT_NT_BARLTBASE1,	IDT_NT_BARUTBASE1},
	  {IDT_NT_BARSETUP2,	IDT_NT_BARLIMIT2,
	   IDT_NT_BARLTBASE2,	IDT_NT_BARUTBASE2},
	  {IDT_NT_BARSETUP3,	IDT_NT_BARLIMIT3,
	   IDT_NT_BARLTBASE3,	IDT_NT_BARUTBASE3},
	  {IDT_NT_BARSETUP4,	IDT_NT_BARLIMIT4,
	   IDT_NT_BARLTBASE4,	IDT_NT_BARUTBASE4},
	  {IDT_NT_BARSETUP5,	IDT_NT_BARLIMIT5,
	   IDT_NT_BARLTBASE5,	IDT_NT_BARUTBASE5} },
	{ {IDT_NT_INMSG0,	IDT_NT_OUTMSG0,	IDT_NT_INMSGSRC0},
	  {IDT_NT_INMSG1,	IDT_NT_OUTMSG1,	IDT_NT_INMSGSRC1},
	  {IDT_NT_INMSG2,	IDT_NT_OUTMSG2,	IDT_NT_INMSGSRC2},
	  {IDT_NT_INMSG3,	IDT_NT_OUTMSG3,	IDT_NT_INMSGSRC3} }
};

/*
 * NT Endpoint ports data table with the corresponding pcie command, link
 * status, control and BAR-related registers
 */
static const struct idt_ntb_port portdata_tbl[IDT_MAX_NR_PORTS] = {
/*0*/	{ IDT_SW_NTP0_PCIECMDSTS,	IDT_SW_NTP0_PCIELCTLSTS,
	  IDT_SW_NTP0_NTCTL,
	  IDT_SW_SWPORT0CTL,		IDT_SW_SWPORT0STS,
	  { {IDT_SW_NTP0_BARSETUP0,	IDT_SW_NTP0_BARLIMIT0,
	     IDT_SW_NTP0_BARLTBASE0,	IDT_SW_NTP0_BARUTBASE0},
	    {IDT_SW_NTP0_BARSETUP1,	IDT_SW_NTP0_BARLIMIT1,
	     IDT_SW_NTP0_BARLTBASE1,	IDT_SW_NTP0_BARUTBASE1},
	    {IDT_SW_NTP0_BARSETUP2,	IDT_SW_NTP0_BARLIMIT2,
	     IDT_SW_NTP0_BARLTBASE2,	IDT_SW_NTP0_BARUTBASE2},
	    {IDT_SW_NTP0_BARSETUP3,	IDT_SW_NTP0_BARLIMIT3,
	     IDT_SW_NTP0_BARLTBASE3,	IDT_SW_NTP0_BARUTBASE3},
	    {IDT_SW_NTP0_BARSETUP4,	IDT_SW_NTP0_BARLIMIT4,
	     IDT_SW_NTP0_BARLTBASE4,	IDT_SW_NTP0_BARUTBASE4},
	    {IDT_SW_NTP0_BARSETUP5,	IDT_SW_NTP0_BARLIMIT5,
	     IDT_SW_NTP0_BARLTBASE5,	IDT_SW_NTP0_BARUTBASE5} } },
/*1*/	{0},
/*2*/	{ IDT_SW_NTP2_PCIECMDSTS,	IDT_SW_NTP2_PCIELCTLSTS,
	  IDT_SW_NTP2_NTCTL,
	  IDT_SW_SWPORT2CTL,		IDT_SW_SWPORT2STS,
	  { {IDT_SW_NTP2_BARSETUP0,	IDT_SW_NTP2_BARLIMIT0,
	     IDT_SW_NTP2_BARLTBASE0,	IDT_SW_NTP2_BARUTBASE0},
	    {IDT_SW_NTP2_BARSETUP1,	IDT_SW_NTP2_BARLIMIT1,
	     IDT_SW_NTP2_BARLTBASE1,	IDT_SW_NTP2_BARUTBASE1},
	    {IDT_SW_NTP2_BARSETUP2,	IDT_SW_NTP2_BARLIMIT2,
	     IDT_SW_NTP2_BARLTBASE2,	IDT_SW_NTP2_BARUTBASE2},
	    {IDT_SW_NTP2_BARSETUP3,	IDT_SW_NTP2_BARLIMIT3,
	     IDT_SW_NTP2_BARLTBASE3,	IDT_SW_NTP2_BARUTBASE3},
	    {IDT_SW_NTP2_BARSETUP4,	IDT_SW_NTP2_BARLIMIT4,
	     IDT_SW_NTP2_BARLTBASE4,	IDT_SW_NTP2_BARUTBASE4},
	    {IDT_SW_NTP2_BARSETUP5,	IDT_SW_NTP2_BARLIMIT5,
	     IDT_SW_NTP2_BARLTBASE5,	IDT_SW_NTP2_BARUTBASE5} } },
/*3*/	{0},
/*4*/	{ IDT_SW_NTP4_PCIECMDSTS,	IDT_SW_NTP4_PCIELCTLSTS,
	  IDT_SW_NTP4_NTCTL,
	  IDT_SW_SWPORT4CTL,		IDT_SW_SWPORT4STS,
	  { {IDT_SW_NTP4_BARSETUP0,	IDT_SW_NTP4_BARLIMIT0,
	     IDT_SW_NTP4_BARLTBASE0,	IDT_SW_NTP4_BARUTBASE0},
	    {IDT_SW_NTP4_BARSETUP1,	IDT_SW_NTP4_BARLIMIT1,
	     IDT_SW_NTP4_BARLTBASE1,	IDT_SW_NTP4_BARUTBASE1},
	    {IDT_SW_NTP4_BARSETUP2,	IDT_SW_NTP4_BARLIMIT2,
	     IDT_SW_NTP4_BARLTBASE2,	IDT_SW_NTP4_BARUTBASE2},
	    {IDT_SW_NTP4_BARSETUP3,	IDT_SW_NTP4_BARLIMIT3,
	     IDT_SW_NTP4_BARLTBASE3,	IDT_SW_NTP4_BARUTBASE3},
	    {IDT_SW_NTP4_BARSETUP4,	IDT_SW_NTP4_BARLIMIT4,
	     IDT_SW_NTP4_BARLTBASE4,	IDT_SW_NTP4_BARUTBASE4},
	    {IDT_SW_NTP4_BARSETUP5,	IDT_SW_NTP4_BARLIMIT5,
	     IDT_SW_NTP4_BARLTBASE5,	IDT_SW_NTP4_BARUTBASE5} } },
/*5*/	{0},
/*6*/	{ IDT_SW_NTP6_PCIECMDSTS,	IDT_SW_NTP6_PCIELCTLSTS,
	  IDT_SW_NTP6_NTCTL,
	  IDT_SW_SWPORT6CTL,		IDT_SW_SWPORT6STS,
	  { {IDT_SW_NTP6_BARSETUP0,	IDT_SW_NTP6_BARLIMIT0,
	     IDT_SW_NTP6_BARLTBASE0,	IDT_SW_NTP6_BARUTBASE0},
	    {IDT_SW_NTP6_BARSETUP1,	IDT_SW_NTP6_BARLIMIT1,
	     IDT_SW_NTP6_BARLTBASE1,	IDT_SW_NTP6_BARUTBASE1},
	    {IDT_SW_NTP6_BARSETUP2,	IDT_SW_NTP6_BARLIMIT2,
	     IDT_SW_NTP6_BARLTBASE2,	IDT_SW_NTP6_BARUTBASE2},
	    {IDT_SW_NTP6_BARSETUP3,	IDT_SW_NTP6_BARLIMIT3,
	     IDT_SW_NTP6_BARLTBASE3,	IDT_SW_NTP6_BARUTBASE3},
	    {IDT_SW_NTP6_BARSETUP4,	IDT_SW_NTP6_BARLIMIT4,
	     IDT_SW_NTP6_BARLTBASE4,	IDT_SW_NTP6_BARUTBASE4},
	    {IDT_SW_NTP6_BARSETUP5,	IDT_SW_NTP6_BARLIMIT5,
	     IDT_SW_NTP6_BARLTBASE5,	IDT_SW_NTP6_BARUTBASE5} } },
/*7*/	{0},
/*8*/	{ IDT_SW_NTP8_PCIECMDSTS,	IDT_SW_NTP8_PCIELCTLSTS,
	  IDT_SW_NTP8_NTCTL,
	  IDT_SW_SWPORT8CTL,		IDT_SW_SWPORT8STS,
	  { {IDT_SW_NTP8_BARSETUP0,	IDT_SW_NTP8_BARLIMIT0,
	     IDT_SW_NTP8_BARLTBASE0,	IDT_SW_NTP8_BARUTBASE0},
	    {IDT_SW_NTP8_BARSETUP1,	IDT_SW_NTP8_BARLIMIT1,
	     IDT_SW_NTP8_BARLTBASE1,	IDT_SW_NTP8_BARUTBASE1},
	    {IDT_SW_NTP8_BARSETUP2,	IDT_SW_NTP8_BARLIMIT2,
	     IDT_SW_NTP8_BARLTBASE2,	IDT_SW_NTP8_BARUTBASE2},
	    {IDT_SW_NTP8_BARSETUP3,	IDT_SW_NTP8_BARLIMIT3,
	     IDT_SW_NTP8_BARLTBASE3,	IDT_SW_NTP8_BARUTBASE3},
	    {IDT_SW_NTP8_BARSETUP4,	IDT_SW_NTP8_BARLIMIT4,
	     IDT_SW_NTP8_BARLTBASE4,	IDT_SW_NTP8_BARUTBASE4},
	    {IDT_SW_NTP8_BARSETUP5,	IDT_SW_NTP8_BARLIMIT5,
	     IDT_SW_NTP8_BARLTBASE5,	IDT_SW_NTP8_BARUTBASE5} } },
/*9*/	{0},
/*10*/	{0},
/*11*/	{0},
/*12*/	{ IDT_SW_NTP12_PCIECMDSTS,	IDT_SW_NTP12_PCIELCTLSTS,
	  IDT_SW_NTP12_NTCTL,
	  IDT_SW_SWPORT12CTL,		IDT_SW_SWPORT12STS,
	  { {IDT_SW_NTP12_BARSETUP0,	IDT_SW_NTP12_BARLIMIT0,
	     IDT_SW_NTP12_BARLTBASE0,	IDT_SW_NTP12_BARUTBASE0},
	    {IDT_SW_NTP12_BARSETUP1,	IDT_SW_NTP12_BARLIMIT1,
	     IDT_SW_NTP12_BARLTBASE1,	IDT_SW_NTP12_BARUTBASE1},
	    {IDT_SW_NTP12_BARSETUP2,	IDT_SW_NTP12_BARLIMIT2,
	     IDT_SW_NTP12_BARLTBASE2,	IDT_SW_NTP12_BARUTBASE2},
	    {IDT_SW_NTP12_BARSETUP3,	IDT_SW_NTP12_BARLIMIT3,
	     IDT_SW_NTP12_BARLTBASE3,	IDT_SW_NTP12_BARUTBASE3},
	    {IDT_SW_NTP12_BARSETUP4,	IDT_SW_NTP12_BARLIMIT4,
	     IDT_SW_NTP12_BARLTBASE4,	IDT_SW_NTP12_BARUTBASE4},
	    {IDT_SW_NTP12_BARSETUP5,	IDT_SW_NTP12_BARLIMIT5,
	     IDT_SW_NTP12_BARLTBASE5,	IDT_SW_NTP12_BARUTBASE5} } },
/*13*/	{0},
/*14*/	{0},
/*15*/	{0},
/*16*/	{ IDT_SW_NTP16_PCIECMDSTS,	IDT_SW_NTP16_PCIELCTLSTS,
	  IDT_SW_NTP16_NTCTL,
	  IDT_SW_SWPORT16CTL,		IDT_SW_SWPORT16STS,
	  { {IDT_SW_NTP16_BARSETUP0,	IDT_SW_NTP16_BARLIMIT0,
	     IDT_SW_NTP16_BARLTBASE0,	IDT_SW_NTP16_BARUTBASE0},
	    {IDT_SW_NTP16_BARSETUP1,	IDT_SW_NTP16_BARLIMIT1,
	     IDT_SW_NTP16_BARLTBASE1,	IDT_SW_NTP16_BARUTBASE1},
	    {IDT_SW_NTP16_BARSETUP2,	IDT_SW_NTP16_BARLIMIT2,
	     IDT_SW_NTP16_BARLTBASE2,	IDT_SW_NTP16_BARUTBASE2},
	    {IDT_SW_NTP16_BARSETUP3,	IDT_SW_NTP16_BARLIMIT3,
	     IDT_SW_NTP16_BARLTBASE3,	IDT_SW_NTP16_BARUTBASE3},
	    {IDT_SW_NTP16_BARSETUP4,	IDT_SW_NTP16_BARLIMIT4,
	     IDT_SW_NTP16_BARLTBASE4,	IDT_SW_NTP16_BARUTBASE4},
	    {IDT_SW_NTP16_BARSETUP5,	IDT_SW_NTP16_BARLIMIT5,
	     IDT_SW_NTP16_BARLTBASE5,	IDT_SW_NTP16_BARUTBASE5} } },
/*17*/	{0},
/*18*/	{0},
/*19*/	{0},
/*20*/	{ IDT_SW_NTP20_PCIECMDSTS,	IDT_SW_NTP20_PCIELCTLSTS,
	  IDT_SW_NTP20_NTCTL,
	  IDT_SW_SWPORT20CTL,		IDT_SW_SWPORT20STS,
	  { {IDT_SW_NTP20_BARSETUP0,	IDT_SW_NTP20_BARLIMIT0,
	     IDT_SW_NTP20_BARLTBASE0,	IDT_SW_NTP20_BARUTBASE0},
	    {IDT_SW_NTP20_BARSETUP1,	IDT_SW_NTP20_BARLIMIT1,
	     IDT_SW_NTP20_BARLTBASE1,	IDT_SW_NTP20_BARUTBASE1},
	    {IDT_SW_NTP20_BARSETUP2,	IDT_SW_NTP20_BARLIMIT2,
	     IDT_SW_NTP20_BARLTBASE2,	IDT_SW_NTP20_BARUTBASE2},
	    {IDT_SW_NTP20_BARSETUP3,	IDT_SW_NTP20_BARLIMIT3,
	     IDT_SW_NTP20_BARLTBASE3,	IDT_SW_NTP20_BARUTBASE3},
	    {IDT_SW_NTP20_BARSETUP4,	IDT_SW_NTP20_BARLIMIT4,
	     IDT_SW_NTP20_BARLTBASE4,	IDT_SW_NTP20_BARUTBASE4},
	    {IDT_SW_NTP20_BARSETUP5,	IDT_SW_NTP20_BARLIMIT5,
	     IDT_SW_NTP20_BARLTBASE5,	IDT_SW_NTP20_BARUTBASE5} } },
/*21*/	{0},
/*22*/	{0},
/*23*/	{0}
};

/*
 * IDT PCIe-switch partitions table with the corresponding control, status
 * and messages control registers
 */
static const struct idt_ntb_part partdata_tbl[IDT_MAX_NR_PARTS] = {
/*0*/	{ IDT_SW_SWPART0CTL,	IDT_SW_SWPART0STS,
	  {IDT_SW_SWP0MSGCTL0,	IDT_SW_SWP0MSGCTL1,
	   IDT_SW_SWP0MSGCTL2,	IDT_SW_SWP0MSGCTL3} },
/*1*/	{ IDT_SW_SWPART1CTL,	IDT_SW_SWPART1STS,
	  {IDT_SW_SWP1MSGCTL0,	IDT_SW_SWP1MSGCTL1,
	   IDT_SW_SWP1MSGCTL2,	IDT_SW_SWP1MSGCTL3} },
/*2*/	{ IDT_SW_SWPART2CTL,	IDT_SW_SWPART2STS,
	  {IDT_SW_SWP2MSGCTL0,	IDT_SW_SWP2MSGCTL1,
	   IDT_SW_SWP2MSGCTL2,	IDT_SW_SWP2MSGCTL3} },
/*3*/	{ IDT_SW_SWPART3CTL,	IDT_SW_SWPART3STS,
	  {IDT_SW_SWP3MSGCTL0,	IDT_SW_SWP3MSGCTL1,
	   IDT_SW_SWP3MSGCTL2,	IDT_SW_SWP3MSGCTL3} },
/*4*/	{ IDT_SW_SWPART4CTL,	IDT_SW_SWPART4STS,
	  {IDT_SW_SWP4MSGCTL0,	IDT_SW_SWP4MSGCTL1,
	   IDT_SW_SWP4MSGCTL2,	IDT_SW_SWP4MSGCTL3} },
/*5*/	{ IDT_SW_SWPART5CTL,	IDT_SW_SWPART5STS,
	  {IDT_SW_SWP5MSGCTL0,	IDT_SW_SWP5MSGCTL1,
	   IDT_SW_SWP5MSGCTL2,	IDT_SW_SWP5MSGCTL3} },
/*6*/	{ IDT_SW_SWPART6CTL,	IDT_SW_SWPART6STS,
	  {IDT_SW_SWP6MSGCTL0,	IDT_SW_SWP6MSGCTL1,
	   IDT_SW_SWP6MSGCTL2,	IDT_SW_SWP6MSGCTL3} },
/*7*/	{ IDT_SW_SWPART7CTL,	IDT_SW_SWPART7STS,
	  {IDT_SW_SWP7MSGCTL0,	IDT_SW_SWP7MSGCTL1,
	   IDT_SW_SWP7MSGCTL2,	IDT_SW_SWP7MSGCTL3} }
};

/*
 * DebugFS directory to place the driver debug file
 */
static struct dentry *dbgfs_topdir;

/*=============================================================================
 *                1. IDT PCIe-switch registers IO-functions
 *
 *    Beside ordinary configuration space registers IDT PCIe-switch expose
 * global configuration registers, which are used to determine state of other
 * device ports as well as being notified of some switch-related events.
 * Additionally all the configuration space registers of all the IDT
 * PCIe-switch functions are mapped to the Global Address space, so each
 * function can determine a configuration of any other PCI-function.
 *    Functions declared in this chapter are created to encapsulate access
 * to configuration and global registers, so the driver code just need to
 * provide IDT NTB hardware descriptor and a register address.
 *=============================================================================
 */

/*
 * idt_nt_write() - PCI configuration space registers write method
 * @ndev:	IDT NTB hardware driver descriptor
 * @reg:	Register to write data to
 * @data:	Value to write to the register
 *
 * IDT PCIe-switch registers are all Little endian.
 */
static void idt_nt_write(struct idt_ntb_dev *ndev,
			 const unsigned int reg, const u32 data)
{
	/*
	 * It's obvious bug to request a register exceeding the maximum possible
	 * value as well as to have it unaligned.
	 */
	if (WARN_ON(reg > IDT_REG_PCI_MAX || !IS_ALIGNED(reg, IDT_REG_ALIGN)))
		return;

	/* Just write the value to the specified register */
	iowrite32(data, ndev->cfgspc + (ptrdiff_t)reg);
}

/*
 * idt_nt_read() - PCI configuration space registers read method
 * @ndev:	IDT NTB hardware driver descriptor
 * @reg:	Register to write data to
 *
 * IDT PCIe-switch Global configuration registers are all Little endian.
 *
 * Return: register value
 */
static u32 idt_nt_read(struct idt_ntb_dev *ndev, const unsigned int reg)
{
	/*
	 * It's obvious bug to request a register exceeding the maximum possible
	 * value as well as to have it unaligned.
	 */
	if (WARN_ON(reg > IDT_REG_PCI_MAX || !IS_ALIGNED(reg, IDT_REG_ALIGN)))
		return ~0;

	/* Just read the value from the specified register */
	return ioread32(ndev->cfgspc + (ptrdiff_t)reg);
}

/*
 * idt_sw_write() - Global registers write method
 * @ndev:	IDT NTB hardware driver descriptor
 * @reg:	Register to write data to
 * @data:	Value to write to the register
 *
 * IDT PCIe-switch Global configuration registers are all Little endian.
 */
static void idt_sw_write(struct idt_ntb_dev *ndev,
			 const unsigned int reg, const u32 data)
{
	unsigned long irqflags;

	/*
	 * It's obvious bug to request a register exceeding the maximum possible
	 * value as well as to have it unaligned.
	 */
	if (WARN_ON(reg > IDT_REG_SW_MAX || !IS_ALIGNED(reg, IDT_REG_ALIGN)))
		return;

	/* Lock GASA registers operations */
	spin_lock_irqsave(&ndev->gasa_lock, irqflags);
	/* Set the global register address */
	iowrite32((u32)reg, ndev->cfgspc + (ptrdiff_t)IDT_NT_GASAADDR);
	/* Put the new value of the register */
	iowrite32(data, ndev->cfgspc + (ptrdiff_t)IDT_NT_GASADATA);
	/* Make sure the PCIe transactions are executed */
	mmiowb();
	/* Unlock GASA registers operations */
	spin_unlock_irqrestore(&ndev->gasa_lock, irqflags);
}

/*
 * idt_sw_read() - Global registers read method
 * @ndev:	IDT NTB hardware driver descriptor
 * @reg:	Register to write data to
 *
 * IDT PCIe-switch Global configuration registers are all Little endian.
 *
 * Return: register value
 */
static u32 idt_sw_read(struct idt_ntb_dev *ndev, const unsigned int reg)
{
	unsigned long irqflags;
	u32 data;

	/*
	 * It's obvious bug to request a register exceeding the maximum possible
	 * value as well as to have it unaligned.
	 */
	if (WARN_ON(reg > IDT_REG_SW_MAX || !IS_ALIGNED(reg, IDT_REG_ALIGN)))
		return ~0;

	/* Lock GASA registers operations */
	spin_lock_irqsave(&ndev->gasa_lock, irqflags);
	/* Set the global register address */
	iowrite32((u32)reg, ndev->cfgspc + (ptrdiff_t)IDT_NT_GASAADDR);
	/* Get the data of the register (read ops acts as MMIO barrier) */
	data = ioread32(ndev->cfgspc + (ptrdiff_t)IDT_NT_GASADATA);
	/* Unlock GASA registers operations */
	spin_unlock_irqrestore(&ndev->gasa_lock, irqflags);

	return data;
}

/*
 * idt_reg_set_bits() - set bits of a passed register
 * @ndev:	IDT NTB hardware driver descriptor
 * @reg:	Register to change bits of
 * @reg_lock:	Register access spin lock
 * @valid_mask:	Mask of valid bits
 * @set_bits:	Bitmask to set
 *
 * Helper method to check whether a passed bitfield is valid and set
 * corresponding bits of a register.
 *
 * WARNING! Make sure the passed register isn't accessed over plane
 * idt_nt_write() method (read method is ok to be used concurrently).
 *
 * Return: zero on success, negative error on invalid bitmask.
 */
static inline int idt_reg_set_bits(struct idt_ntb_dev *ndev, unsigned int reg,
				   spinlock_t *reg_lock,
				   u64 valid_mask, u64 set_bits)
{
	unsigned long irqflags;
	u32 data;

	if (set_bits & ~(u64)valid_mask)
		return -EINVAL;

	/* Lock access to the register unless the change is written back */
	spin_lock_irqsave(reg_lock, irqflags);
	data = idt_nt_read(ndev, reg) | (u32)set_bits;
	idt_nt_write(ndev, reg, data);
	/* Unlock the register */
	spin_unlock_irqrestore(reg_lock, irqflags);

	return 0;
}

/*
 * idt_reg_clear_bits() - clear bits of a passed register
 * @ndev:	IDT NTB hardware driver descriptor
 * @reg:	Register to change bits of
 * @reg_lock:	Register access spin lock
 * @set_bits:	Bitmask to clear
 *
 * Helper method to check whether a passed bitfield is valid and clear
 * corresponding bits of a register.
 *
 * NOTE! Invalid bits are always considered cleared so it's not an error
 * to clear them over.
 *
 * WARNING! Make sure the passed register isn't accessed over plane
 * idt_nt_write() method (read method is ok to use concurrently).
 */
static inline void idt_reg_clear_bits(struct idt_ntb_dev *ndev,
				     unsigned int reg, spinlock_t *reg_lock,
				     u64 clear_bits)
{
	unsigned long irqflags;
	u32 data;

	/* Lock access to the register unless the change is written back */
	spin_lock_irqsave(reg_lock, irqflags);
	data = idt_nt_read(ndev, reg) & ~(u32)clear_bits;
	idt_nt_write(ndev, reg, data);
	/* Unlock the register */
	spin_unlock_irqrestore(reg_lock, irqflags);
}

/*===========================================================================
 *                           2. Ports operations
 *
 *    IDT PCIe-switches can have from 3 up to 8 ports with possible
 * NT-functions enabled. So all the possible ports need to be scanned looking
 * for NTB activated. NTB API will have enumerated only the ports with NTB.
 *===========================================================================
 */

/*
 * idt_scan_ports() - scan IDT PCIe-switch ports collecting info in the tables
 * @ndev:	Pointer to the PCI device descriptor
 *
 * Return: zero on success, otherwise a negative error number.
 */
static int idt_scan_ports(struct idt_ntb_dev *ndev)
{
	unsigned char pidx, port, part;
	u32 data, portsts, partsts;

	/* Retrieve the local port number */
	data = idt_nt_read(ndev, IDT_NT_PCIELCAP);
	ndev->port = GET_FIELD(PCIELCAP_PORTNUM, data);

	/* Retrieve the local partition number */
	portsts = idt_sw_read(ndev, portdata_tbl[ndev->port].sts);
	ndev->part = GET_FIELD(SWPORTxSTS_SWPART, portsts);

	/* Initialize port/partition -> index tables with invalid values */
	memset(ndev->port_idx_map, -EINVAL, sizeof(ndev->port_idx_map));
	memset(ndev->part_idx_map, -EINVAL, sizeof(ndev->part_idx_map));

	/*
	 * Walk over all the possible ports checking whether any of them has
	 * NT-function activated
	 */
	ndev->peer_cnt = 0;
	for (pidx = 0; pidx < ndev->swcfg->port_cnt; pidx++) {
		port = ndev->swcfg->ports[pidx];
		/* Skip local port */
		if (port == ndev->port)
			continue;

		/* Read the port status register to get it partition */
		portsts = idt_sw_read(ndev, portdata_tbl[port].sts);
		part = GET_FIELD(SWPORTxSTS_SWPART, portsts);

		/* Retrieve the partition status */
		partsts = idt_sw_read(ndev, partdata_tbl[part].sts);
		/* Check if partition state is active and port has NTB */
		if (IS_FLD_SET(SWPARTxSTS_STATE, partsts, ACT) &&
		    (IS_FLD_SET(SWPORTxSTS_MODE, portsts, NT) ||
		     IS_FLD_SET(SWPORTxSTS_MODE, portsts, USNT) ||
		     IS_FLD_SET(SWPORTxSTS_MODE, portsts, USNTDMA) ||
		     IS_FLD_SET(SWPORTxSTS_MODE, portsts, NTDMA))) {
			/* Save the port and partition numbers */
			ndev->peers[ndev->peer_cnt].port = port;
			ndev->peers[ndev->peer_cnt].part = part;
			/* Fill in the port/partition -> index tables */
			ndev->port_idx_map[port] = ndev->peer_cnt;
			ndev->part_idx_map[part] = ndev->peer_cnt;
			ndev->peer_cnt++;
		}
	}

	dev_dbg(&ndev->ntb.pdev->dev, "Local port: %hhu, num of peers: %hhu\n",
		ndev->port, ndev->peer_cnt);

	/* It's useless to have this driver loaded if there is no any peer */
	if (ndev->peer_cnt == 0) {
		dev_warn(&ndev->ntb.pdev->dev, "No active peer found\n");
		return -ENODEV;
	}

	return 0;
}

/*
 * idt_ntb_port_number() - get the local port number
 * @ntb:	NTB device context.
 *
 * Return: the local port number
 */
static int idt_ntb_port_number(struct ntb_dev *ntb)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	return ndev->port;
}

/*
 * idt_ntb_peer_port_count() - get the number of peer ports
 * @ntb:	NTB device context.
 *
 * Return the count of detected peer NT-functions.
 *
 * Return: number of peer ports
 */
static int idt_ntb_peer_port_count(struct ntb_dev *ntb)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	return ndev->peer_cnt;
}

/*
 * idt_ntb_peer_port_number() - get peer port by given index
 * @ntb:	NTB device context.
 * @pidx:	Peer port index.
 *
 * Return: peer port or negative error
 */
static int idt_ntb_peer_port_number(struct ntb_dev *ntb, int pidx)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	if (pidx < 0 || ndev->peer_cnt <= pidx)
		return -EINVAL;

	/* Return the detected NT-function port number */
	return ndev->peers[pidx].port;
}

/*
 * idt_ntb_peer_port_idx() - get peer port index by given port number
 * @ntb:	NTB device context.
 * @port:	Peer port number.
 *
 * Internal port -> index table is pre-initialized with -EINVAL values,
 * so we just need to return it value
 *
 * Return: peer NT-function port index or negative error
 */
static int idt_ntb_peer_port_idx(struct ntb_dev *ntb, int port)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	if (port < 0 || IDT_MAX_NR_PORTS <= port)
		return -EINVAL;

	return ndev->port_idx_map[port];
}

/*===========================================================================
 *                         3. Link status operations
 *    There is no any ready-to-use method to have peer ports notified if NTB
 * link is set up or got down. Instead global signal can be used instead.
 * In case if any one of ports changes local NTB link state, it sends
 * global signal and clears corresponding global state bit. Then all the ports
 * receive a notification of that, so to make client driver being aware of
 * possible NTB link change.
 *    Additionally each of active NT-functions is subscribed to PCIe-link
 * state changes of peer ports.
 *===========================================================================
 */

static void idt_ntb_local_link_disable(struct idt_ntb_dev *ndev);

/*
 * idt_init_link() - Initialize NTB link state notification subsystem
 * @ndev:	IDT NTB hardware driver descriptor
 *
 * Function performs the basic initialization of some global registers
 * needed to enable IRQ-based notifications of PCIe Link Up/Down and
 * Global Signal events.
 * NOTE Since it's not possible to determine when all the NTB peer drivers are
 * unloaded as well as have those registers accessed concurrently, we must
 * preinitialize them with the same value and leave it uncleared on local
 * driver unload.
 */
static void idt_init_link(struct idt_ntb_dev *ndev)
{
	u32 part_mask, port_mask, se_mask;
	unsigned char pidx;

	/* Initialize spin locker of Mapping Table access registers */
	spin_lock_init(&ndev->mtbl_lock);

	/* Walk over all detected peers collecting port and partition masks */
	port_mask = ~BIT(ndev->port);
	part_mask = ~BIT(ndev->part);
	for (pidx = 0; pidx < ndev->peer_cnt; pidx++) {
		port_mask &= ~BIT(ndev->peers[pidx].port);
		part_mask &= ~BIT(ndev->peers[pidx].part);
	}

	/* Clean the Link Up/Down and GLobal Signal status registers */
	idt_sw_write(ndev, IDT_SW_SELINKUPSTS, (u32)-1);
	idt_sw_write(ndev, IDT_SW_SELINKDNSTS, (u32)-1);
	idt_sw_write(ndev, IDT_SW_SEGSIGSTS, (u32)-1);

	/* Unmask NT-activated partitions to receive Global Switch events */
	idt_sw_write(ndev, IDT_SW_SEPMSK, part_mask);

	/* Enable PCIe Link Up events of NT-activated ports */
	idt_sw_write(ndev, IDT_SW_SELINKUPMSK, port_mask);

	/* Enable PCIe Link Down events of NT-activated ports */
	idt_sw_write(ndev, IDT_SW_SELINKDNMSK, port_mask);

	/* Unmask NT-activated partitions to receive Global Signal events */
	idt_sw_write(ndev, IDT_SW_SEGSIGMSK, part_mask);

	/* Unmask Link Up/Down and Global Switch Events */
	se_mask = ~(IDT_SEMSK_LINKUP | IDT_SEMSK_LINKDN | IDT_SEMSK_GSIGNAL);
	idt_sw_write(ndev, IDT_SW_SEMSK, se_mask);

	dev_dbg(&ndev->ntb.pdev->dev, "NTB link status events initialized");
}

/*
 * idt_deinit_link() - deinitialize link subsystem
 * @ndev:	IDT NTB hardware driver descriptor
 *
 * Just disable the link back.
 */
static void idt_deinit_link(struct idt_ntb_dev *ndev)
{
	/* Disable the link */
	idt_ntb_local_link_disable(ndev);

	dev_dbg(&ndev->ntb.pdev->dev, "NTB link status events deinitialized");
}

/*
 * idt_se_isr() - switch events ISR
 * @ndev:	IDT NTB hardware driver descriptor
 * @ntint_sts:	NT-function interrupt status
 *
 * This driver doesn't support IDT PCIe-switch dynamic reconfigurations,
 * Failover capability, etc, so switch events are utilized to notify of
 * PCIe and NTB link events.
 * The method is called from PCIe ISR bottom-half routine.
 */
static void idt_se_isr(struct idt_ntb_dev *ndev, u32 ntint_sts)
{
	u32 sests;

	/* Read Switch Events status */
	sests = idt_sw_read(ndev, IDT_SW_SESTS);

	/* Clean the Link Up/Down and Global Signal status registers */
	idt_sw_write(ndev, IDT_SW_SELINKUPSTS, (u32)-1);
	idt_sw_write(ndev, IDT_SW_SELINKDNSTS, (u32)-1);
	idt_sw_write(ndev, IDT_SW_SEGSIGSTS, (u32)-1);

	/* Clean the corresponding interrupt bit */
	idt_nt_write(ndev, IDT_NT_NTINTSTS, IDT_NTINTSTS_SEVENT);

	dev_dbg(&ndev->ntb.pdev->dev, "SE IRQ detected %#08x (SESTS %#08x)",
			  ntint_sts, sests);

	/* Notify the client driver of possible link state change */
	ntb_link_event(&ndev->ntb);
}

/*
 * idt_ntb_local_link_enable() - enable the local NTB link.
 * @ndev:	IDT NTB hardware driver descriptor
 *
 * In order to enable the NTB link we need:
 * - enable Completion TLPs translation
 * - initialize mapping table to enable the Request ID translation
 * - notify peers of NTB link state change
 */
static void idt_ntb_local_link_enable(struct idt_ntb_dev *ndev)
{
	u32 reqid, mtbldata = 0;
	unsigned long irqflags;

	/* Enable the ID protection and Completion TLPs translation */
	idt_nt_write(ndev, IDT_NT_NTCTL, IDT_NTCTL_CPEN);

	/* Retrieve the current Requester ID (Bus:Device:Function) */
	reqid = idt_nt_read(ndev, IDT_NT_REQIDCAP);

	/*
	 * Set the corresponding NT Mapping table entry of port partition index
	 * with the data to perform the Request ID translation
	 */
	mtbldata = SET_FIELD(NTMTBLDATA_REQID, 0, reqid) |
		   SET_FIELD(NTMTBLDATA_PART, 0, ndev->part) |
		   IDT_NTMTBLDATA_VALID;
	spin_lock_irqsave(&ndev->mtbl_lock, irqflags);
	idt_nt_write(ndev, IDT_NT_NTMTBLADDR, ndev->part);
	idt_nt_write(ndev, IDT_NT_NTMTBLDATA, mtbldata);
	mmiowb();
	spin_unlock_irqrestore(&ndev->mtbl_lock, irqflags);

	/* Notify the peers by setting and clearing the global signal bit */
	idt_nt_write(ndev, IDT_NT_NTGSIGNAL, IDT_NTGSIGNAL_SET);
	idt_sw_write(ndev, IDT_SW_SEGSIGSTS, (u32)1 << ndev->part);
}

/*
 * idt_ntb_local_link_disable() - disable the local NTB link.
 * @ndev:	IDT NTB hardware driver descriptor
 *
 * In order to enable the NTB link we need:
 * - disable Completion TLPs translation
 * - clear corresponding mapping table entry
 * - notify peers of NTB link state change
 */
static void idt_ntb_local_link_disable(struct idt_ntb_dev *ndev)
{
	unsigned long irqflags;

	/* Disable Completion TLPs translation */
	idt_nt_write(ndev, IDT_NT_NTCTL, 0);

	/* Clear the corresponding NT Mapping table entry */
	spin_lock_irqsave(&ndev->mtbl_lock, irqflags);
	idt_nt_write(ndev, IDT_NT_NTMTBLADDR, ndev->part);
	idt_nt_write(ndev, IDT_NT_NTMTBLDATA, 0);
	mmiowb();
	spin_unlock_irqrestore(&ndev->mtbl_lock, irqflags);

	/* Notify the peers by setting and clearing the global signal bit */
	idt_nt_write(ndev, IDT_NT_NTGSIGNAL, IDT_NTGSIGNAL_SET);
	idt_sw_write(ndev, IDT_SW_SEGSIGSTS, (u32)1 << ndev->part);
}

/*
 * idt_ntb_local_link_is_up() - test wethter local NTB link is up
 * @ndev:	IDT NTB hardware driver descriptor
 *
 * Local link is up under the following conditions:
 * - Bus mastering is enabled
 * - NTCTL has Completion TLPs translation enabled
 * - Mapping table permits Request TLPs translation
 * NOTE: We don't need to check PCIe link state since it's obviously
 * up while we are able to communicate with IDT PCIe-switch
 *
 * Return: true if link is up, otherwise false
 */
static bool idt_ntb_local_link_is_up(struct idt_ntb_dev *ndev)
{
	unsigned long irqflags;
	u32 data;

	/* Read the local Bus Master Enable status */
	data = idt_nt_read(ndev, IDT_NT_PCICMDSTS);
	if (!(data & IDT_PCICMDSTS_BME))
		return false;

	/* Read the local Completion TLPs translation enable status */
	data = idt_nt_read(ndev, IDT_NT_NTCTL);
	if (!(data & IDT_NTCTL_CPEN))
		return false;

	/* Read Mapping table entry corresponding to the local partition */
	spin_lock_irqsave(&ndev->mtbl_lock, irqflags);
	idt_nt_write(ndev, IDT_NT_NTMTBLADDR, ndev->part);
	data = idt_nt_read(ndev, IDT_NT_NTMTBLDATA);
	spin_unlock_irqrestore(&ndev->mtbl_lock, irqflags);

	return !!(data & IDT_NTMTBLDATA_VALID);
}

/*
 * idt_ntb_peer_link_is_up() - test whether peer NTB link is up
 * @ndev:	IDT NTB hardware driver descriptor
 * @pidx:	Peer port index
 *
 * Peer link is up under the following conditions:
 * - PCIe link is up
 * - Bus mastering is enabled
 * - NTCTL has Completion TLPs translation enabled
 * - Mapping table permits Request TLPs translation
 *
 * Return: true if link is up, otherwise false
 */
static bool idt_ntb_peer_link_is_up(struct idt_ntb_dev *ndev, int pidx)
{
	unsigned long irqflags;
	unsigned char port;
	u32 data;

	/* Retrieve the device port number */
	port = ndev->peers[pidx].port;

	/* Check whether PCIe link is up */
	data = idt_sw_read(ndev, portdata_tbl[port].sts);
	if (!(data & IDT_SWPORTxSTS_LINKUP))
		return false;

	/* Check whether bus mastering is enabled on the peer port */
	data = idt_sw_read(ndev, portdata_tbl[port].pcicmdsts);
	if (!(data & IDT_PCICMDSTS_BME))
		return false;

	/* Check if Completion TLPs translation is enabled on the peer port */
	data = idt_sw_read(ndev, portdata_tbl[port].ntctl);
	if (!(data & IDT_NTCTL_CPEN))
		return false;

	/* Read Mapping table entry corresponding to the peer partition */
	spin_lock_irqsave(&ndev->mtbl_lock, irqflags);
	idt_nt_write(ndev, IDT_NT_NTMTBLADDR, ndev->peers[pidx].part);
	data = idt_nt_read(ndev, IDT_NT_NTMTBLDATA);
	spin_unlock_irqrestore(&ndev->mtbl_lock, irqflags);

	return !!(data & IDT_NTMTBLDATA_VALID);
}

/*
 * idt_ntb_link_is_up() - get the current ntb link state (NTB API callback)
 * @ntb:	NTB device context.
 * @speed:	OUT - The link speed expressed as PCIe generation number.
 * @width:	OUT - The link width expressed as the number of PCIe lanes.
 *
 * Get the bitfield of NTB link states for all peer ports
 *
 * Return: bitfield of indexed ports link state: bit is set/cleared if the
 *         link is up/down respectively.
 */
static u64 idt_ntb_link_is_up(struct ntb_dev *ntb,
			      enum ntb_speed *speed, enum ntb_width *width)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);
	unsigned char pidx;
	u64 status;
	u32 data;

	/* Retrieve the local link speed and width */
	if (speed != NULL || width != NULL) {
		data = idt_nt_read(ndev, IDT_NT_PCIELCTLSTS);
		if (speed != NULL)
			*speed = GET_FIELD(PCIELCTLSTS_CLS, data);
		if (width != NULL)
			*width = GET_FIELD(PCIELCTLSTS_NLW, data);
	}

	/* If local NTB link isn't up then all the links are considered down */
	if (!idt_ntb_local_link_is_up(ndev))
		return 0;

	/* Collect all the peer ports link states into the bitfield */
	status = 0;
	for (pidx = 0; pidx < ndev->peer_cnt; pidx++) {
		if (idt_ntb_peer_link_is_up(ndev, pidx))
			status |= ((u64)1 << pidx);
	}

	return status;
}

/*
 * idt_ntb_link_enable() - enable local port ntb link (NTB API callback)
 * @ntb:	NTB device context.
 * @max_speed:	The maximum link speed expressed as PCIe generation number.
 * @max_width:	The maximum link width expressed as the number of PCIe lanes.
 *
 * Enable just local NTB link. PCIe link parameters are ignored.
 *
 * Return: always zero.
 */
static int idt_ntb_link_enable(struct ntb_dev *ntb, enum ntb_speed speed,
			       enum ntb_width width)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	/* Just enable the local NTB link */
	idt_ntb_local_link_enable(ndev);

	dev_dbg(&ndev->ntb.pdev->dev, "Local NTB link enabled");

	return 0;
}

/*
 * idt_ntb_link_disable() - disable local port ntb link (NTB API callback)
 * @ntb:	NTB device context.
 *
 * Disable just local NTB link.
 *
 * Return: always zero.
 */
static int idt_ntb_link_disable(struct ntb_dev *ntb)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	/* Just disable the local NTB link */
	idt_ntb_local_link_disable(ndev);

	dev_dbg(&ndev->ntb.pdev->dev, "Local NTB link disabled");

	return 0;
}

/*=============================================================================
 *                         4. Memory Window operations
 *
 *    IDT PCIe-switches have two types of memory windows: MWs with direct
 * address translation and MWs with LUT based translation. The first type of
 * MWs is simple map of corresponding BAR address space to a memory space
 * of specified target port. So it implemets just ont-to-one mapping. Lookup
 * table in its turn can map one BAR address space to up to 24 different
 * memory spaces of different ports.
 *    NT-functions BARs can be turned on to implement either direct or lookup
 * table based address translations, so:
 * BAR0 - NT configuration registers space/direct address translation
 * BAR1 - direct address translation/upper address of BAR0x64
 * BAR2 - direct address translation/Lookup table with either 12 or 24 entries
 * BAR3 - direct address translation/upper address of BAR2x64
 * BAR4 - direct address translation/Lookup table with either 12 or 24 entries
 * BAR5 - direct address translation/upper address of BAR4x64
 *    Additionally BAR2 and BAR4 can't have 24-entries LUT enabled at the same
 * time. Since the BARs setup can be rather complicated this driver implements
 * a scanning algorithm to have all the possible memory windows configuration
 * covered.
 *
 * NOTE 1 BAR setup must be done before Linux kernel enumerated NT-function
 * of any port, so this driver would have memory windows configurations fixed.
 * In this way all initializations must be performed either by platform BIOS
 * or using EEPROM connected to IDT PCIe-switch master SMBus.
 *
 * NOTE 2 This driver expects BAR0 mapping NT-function configuration space.
 * Easy calculation can give us an upper boundary of 29 possible memory windows
 * per each NT-function if all the BARs are of 32bit type.
 *=============================================================================
 */

/*
 * idt_get_mw_count() - get memory window count
 * @mw_type:	Memory window type
 *
 * Return: number of memory windows with respect to the BAR type
 */
static inline unsigned char idt_get_mw_count(enum idt_mw_type mw_type)
{
	switch (mw_type) {
	case IDT_MW_DIR:
		return 1;
	case IDT_MW_LUT12:
		return 12;
	case IDT_MW_LUT24:
		return 24;
	default:
		break;
	}

	return 0;
}

/*
 * idt_get_mw_name() - get memory window name
 * @mw_type:	Memory window type
 *
 * Return: pointer to a string with name
 */
static inline char *idt_get_mw_name(enum idt_mw_type mw_type)
{
	switch (mw_type) {
	case IDT_MW_DIR:
		return "DIR  ";
	case IDT_MW_LUT12:
		return "LUT12";
	case IDT_MW_LUT24:
		return "LUT24";
	default:
		break;
	}

	return "unknown";
}

/*
 * idt_scan_mws() - scan memory windows of the port
 * @ndev:	IDT NTB hardware driver descriptor
 * @port:	Port to get number of memory windows for
 * @mw_cnt:	Out - number of memory windows
 *
 * It walks over BAR setup registers of the specified port and determines
 * the memory windows parameters if any activated.
 *
 * Return: array of memory windows
 */
static struct idt_mw_cfg *idt_scan_mws(struct idt_ntb_dev *ndev, int port,
				       unsigned char *mw_cnt)
{
	struct idt_mw_cfg mws[IDT_MAX_NR_MWS], *ret_mws;
	const struct idt_ntb_bar *bars;
	enum idt_mw_type mw_type;
	unsigned char widx, bidx, en_cnt;
	bool bar_64bit = false;
	int aprt_size;
	u32 data;

	/* Retrieve the array of the BARs registers */
	bars = portdata_tbl[port].bars;

	/* Scan all the BARs belonging to the port */
	*mw_cnt = 0;
	for (bidx = 0; bidx < IDT_BAR_CNT; bidx += 1 + bar_64bit) {
		/* Read BARSETUP register value */
		data = idt_sw_read(ndev, bars[bidx].setup);

		/* Skip disabled BARs */
		if (!(data & IDT_BARSETUP_EN)) {
			bar_64bit = false;
			continue;
		}

		/* Skip next BARSETUP if current one has 64bit addressing */
		bar_64bit = IS_FLD_SET(BARSETUP_TYPE, data, 64);

		/* Skip configuration space mapping BARs */
		if (data & IDT_BARSETUP_MODE_CFG)
			continue;

		/* Retrieve MW type/entries count and aperture size */
		mw_type = GET_FIELD(BARSETUP_ATRAN, data);
		en_cnt = idt_get_mw_count(mw_type);
		aprt_size = (u64)1 << GET_FIELD(BARSETUP_SIZE, data);

		/* Save configurations of all available memory windows */
		for (widx = 0; widx < en_cnt; widx++, (*mw_cnt)++) {
			/*
			 * IDT can expose a limited number of MWs, so it's bug
			 * to have more than the driver expects
			 */
			if (*mw_cnt >= IDT_MAX_NR_MWS)
				return ERR_PTR(-EINVAL);

			/* Save basic MW info */
			mws[*mw_cnt].type = mw_type;
			mws[*mw_cnt].bar = bidx;
			mws[*mw_cnt].idx = widx;
			/* It's always DWORD aligned */
			mws[*mw_cnt].addr_align = IDT_TRANS_ALIGN;
			/* DIR and LUT approachs differently configure MWs */
			if (mw_type == IDT_MW_DIR)
				mws[*mw_cnt].size_max = aprt_size;
			else if (mw_type == IDT_MW_LUT12)
				mws[*mw_cnt].size_max = aprt_size / 16;
			else
				mws[*mw_cnt].size_max = aprt_size / 32;
			mws[*mw_cnt].size_align = (mw_type == IDT_MW_DIR) ?
				IDT_DIR_SIZE_ALIGN : mws[*mw_cnt].size_max;
		}
	}

	/* Allocate memory for memory window descriptors */
	ret_mws = devm_kcalloc(&ndev->ntb.pdev->dev, *mw_cnt, sizeof(*ret_mws),
			       GFP_KERNEL);
	if (!ret_mws)
		return ERR_PTR(-ENOMEM);

	/* Copy the info of detected memory windows */
	memcpy(ret_mws, mws, (*mw_cnt)*sizeof(*ret_mws));

	return ret_mws;
}

/*
 * idt_init_mws() - initialize memory windows subsystem
 * @ndev:	IDT NTB hardware driver descriptor
 *
 * Scan BAR setup registers of local and peer ports to determine the
 * outbound and inbound memory windows parameters
 *
 * Return: zero on success, otherwise a negative error number
 */
static int idt_init_mws(struct idt_ntb_dev *ndev)
{
	struct idt_ntb_peer *peer;
	unsigned char pidx;

	/* Scan memory windows of the local port */
	ndev->mws = idt_scan_mws(ndev, ndev->port, &ndev->mw_cnt);
	if (IS_ERR(ndev->mws)) {
		dev_err(&ndev->ntb.pdev->dev,
			"Failed to scan mws of local port %hhu", ndev->port);
		return PTR_ERR(ndev->mws);
	}

	/* Scan memory windows of the peer ports */
	for (pidx = 0; pidx < ndev->peer_cnt; pidx++) {
		peer = &ndev->peers[pidx];
		peer->mws = idt_scan_mws(ndev, peer->port, &peer->mw_cnt);
		if (IS_ERR(peer->mws)) {
			dev_err(&ndev->ntb.pdev->dev,
				"Failed to scan mws of port %hhu", peer->port);
			return PTR_ERR(peer->mws);
		}
	}

	/* Initialize spin locker of the LUT registers */
	spin_lock_init(&ndev->lut_lock);

	dev_dbg(&ndev->ntb.pdev->dev, "Outbound and inbound MWs initialized");

	return 0;
}

/*
 * idt_ntb_mw_count() - number of inbound memory windows (NTB API callback)
 * @ntb:	NTB device context.
 * @pidx:	Port index of peer device.
 *
 * The value is returned for the specified peer, so generally speaking it can
 * be different for different port depending on the IDT PCIe-switch
 * initialization.
 *
 * Return: the number of memory windows.
 */
static int idt_ntb_mw_count(struct ntb_dev *ntb, int pidx)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	if (pidx < 0 || ndev->peer_cnt <= pidx)
		return -EINVAL;

	return ndev->peers[pidx].mw_cnt;
}

/*
 * idt_ntb_mw_get_align() - inbound memory window parameters (NTB API callback)
 * @ntb:	NTB device context.
 * @pidx:	Port index of peer device.
 * @widx:	Memory window index.
 * @addr_align:	OUT - the base alignment for translating the memory window
 * @size_align:	OUT - the size alignment for translating the memory window
 * @size_max:	OUT - the maximum size of the memory window
 *
 * The peer memory window parameters have already been determined, so just
 * return the corresponding values, which mustn't change within session.
 *
 * Return: Zero on success, otherwise a negative error number.
 */
static int idt_ntb_mw_get_align(struct ntb_dev *ntb, int pidx, int widx,
				resource_size_t *addr_align,
				resource_size_t *size_align,
				resource_size_t *size_max)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);
	struct idt_ntb_peer *peer;

	if (pidx < 0 || ndev->peer_cnt <= pidx)
		return -EINVAL;

	peer = &ndev->peers[pidx];

	if (widx < 0 || peer->mw_cnt <= widx)
		return -EINVAL;

	if (addr_align != NULL)
		*addr_align = peer->mws[widx].addr_align;

	if (size_align != NULL)
		*size_align = peer->mws[widx].size_align;

	if (size_max != NULL)
		*size_max = peer->mws[widx].size_max;

	return 0;
}

/*
 * idt_ntb_peer_mw_count() - number of outbound memory windows
 *			     (NTB API callback)
 * @ntb:	NTB device context.
 *
 * Outbound memory windows parameters have been determined based on the
 * BAR setup registers value, which are mostly constants within one session.
 *
 * Return: the number of memory windows.
 */
static int idt_ntb_peer_mw_count(struct ntb_dev *ntb)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	return ndev->mw_cnt;
}

/*
 * idt_ntb_peer_mw_get_addr() - get map address of an outbound memory window
 *				(NTB API callback)
 * @ntb:	NTB device context.
 * @widx:	Memory window index (within ntb_peer_mw_count() return value).
 * @base:	OUT - the base address of mapping region.
 * @size:	OUT - the size of mapping region.
 *
 * Return just parameters of BAR resources mapping. Size reflects just the size
 * of the resource
 *
 * Return: Zero on success, otherwise a negative error number.
 */
static int idt_ntb_peer_mw_get_addr(struct ntb_dev *ntb, int widx,
				    phys_addr_t *base, resource_size_t *size)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	if (widx < 0 || ndev->mw_cnt <= widx)
		return -EINVAL;

	/* Mapping address is just properly shifted BAR resource start */
	if (base != NULL)
		*base = pci_resource_start(ntb->pdev, ndev->mws[widx].bar) +
			ndev->mws[widx].idx * ndev->mws[widx].size_max;

	/* Mapping size has already been calculated at MWs scanning */
	if (size != NULL)
		*size = ndev->mws[widx].size_max;

	return 0;
}

/*
 * idt_ntb_peer_mw_set_trans() - set a translation address of a memory window
 *				 (NTB API callback)
 * @ntb:	NTB device context.
 * @pidx:	Port index of peer device the translation address received from.
 * @widx:	Memory window index.
 * @addr:	The dma address of the shared memory to access.
 * @size:	The size of the shared memory to access.
 *
 * The Direct address translation and LUT base translation is initialized a
 * bit differenet. Although the parameters restriction are now determined by
 * the same code.
 *
 * Return: Zero on success, otherwise an error number.
 */
static int idt_ntb_peer_mw_set_trans(struct ntb_dev *ntb, int pidx, int widx,
				     u64 addr, resource_size_t size)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);
	struct idt_mw_cfg *mw_cfg;
	u32 data = 0, lutoff = 0;

	if (pidx < 0 || ndev->peer_cnt <= pidx)
		return -EINVAL;

	if (widx < 0 || ndev->mw_cnt <= widx)
		return -EINVAL;

	/*
	 * Retrieve the memory window config to make sure the passed arguments
	 * fit it restrictions
	 */
	mw_cfg = &ndev->mws[widx];
	if (!IS_ALIGNED(addr, mw_cfg->addr_align))
		return -EINVAL;
	if (!IS_ALIGNED(size, mw_cfg->size_align) || size > mw_cfg->size_max)
		return -EINVAL;

	/* DIR and LUT based translations are initialized differently */
	if (mw_cfg->type == IDT_MW_DIR) {
		const struct idt_ntb_bar *bar = &ntdata_tbl.bars[mw_cfg->bar];
		u64 limit;
		/* Set destination partition of translation */
		data = idt_nt_read(ndev, bar->setup);
		data = SET_FIELD(BARSETUP_TPART, data, ndev->peers[pidx].part);
		idt_nt_write(ndev, bar->setup, data);
		/* Set translation base address */
		idt_nt_write(ndev, bar->ltbase, (u32)addr);
		idt_nt_write(ndev, bar->utbase, (u32)(addr >> 32));
		/* Set the custom BAR aperture limit */
		limit = pci_resource_start(ntb->pdev, mw_cfg->bar) + size;
		idt_nt_write(ndev, bar->limit, (u32)limit);
		if (IS_FLD_SET(BARSETUP_TYPE, data, 64))
			idt_nt_write(ndev, (bar + 1)->limit, (limit >> 32));
	} else {
		unsigned long irqflags;
		/* Initialize corresponding LUT entry */
		lutoff = SET_FIELD(LUTOFFSET_INDEX, 0, mw_cfg->idx) |
			 SET_FIELD(LUTOFFSET_BAR, 0, mw_cfg->bar);
		data = SET_FIELD(LUTUDATA_PART, 0, ndev->peers[pidx].part) |
			IDT_LUTUDATA_VALID;
		spin_lock_irqsave(&ndev->lut_lock, irqflags);
		idt_nt_write(ndev, IDT_NT_LUTOFFSET, lutoff);
		idt_nt_write(ndev, IDT_NT_LUTLDATA, (u32)addr);
		idt_nt_write(ndev, IDT_NT_LUTMDATA, (u32)(addr >> 32));
		idt_nt_write(ndev, IDT_NT_LUTUDATA, data);
		mmiowb();
		spin_unlock_irqrestore(&ndev->lut_lock, irqflags);
		/* Limit address isn't specified since size is fixed for LUT */
	}

	return 0;
}

/*
 * idt_ntb_peer_mw_clear_trans() - clear the outbound MW translation address
 *				   (NTB API callback)
 * @ntb:	NTB device context.
 * @pidx:	Port index of peer device.
 * @widx:	Memory window index.
 *
 * It effectively disables the translation over the specified outbound MW.
 *
 * Return: Zero on success, otherwise an error number.
 */
static int idt_ntb_peer_mw_clear_trans(struct ntb_dev *ntb, int pidx,
					int widx)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);
	struct idt_mw_cfg *mw_cfg;

	if (pidx < 0 || ndev->peer_cnt <= pidx)
		return -EINVAL;

	if (widx < 0 || ndev->mw_cnt <= widx)
		return -EINVAL;

	mw_cfg = &ndev->mws[widx];

	/* DIR and LUT based translations are initialized differently */
	if (mw_cfg->type == IDT_MW_DIR) {
		const struct idt_ntb_bar *bar = &ntdata_tbl.bars[mw_cfg->bar];
		u32 data;
		/* Read BARSETUP to check BAR type */
		data = idt_nt_read(ndev, bar->setup);
		/* Disable translation by specifying zero BAR limit */
		idt_nt_write(ndev, bar->limit, 0);
		if (IS_FLD_SET(BARSETUP_TYPE, data, 64))
			idt_nt_write(ndev, (bar + 1)->limit, 0);
	} else {
		unsigned long irqflags;
		u32 lutoff;
		/* Clear the corresponding LUT entry up */
		lutoff = SET_FIELD(LUTOFFSET_INDEX, 0, mw_cfg->idx) |
			 SET_FIELD(LUTOFFSET_BAR, 0, mw_cfg->bar);
		spin_lock_irqsave(&ndev->lut_lock, irqflags);
		idt_nt_write(ndev, IDT_NT_LUTOFFSET, lutoff);
		idt_nt_write(ndev, IDT_NT_LUTLDATA, 0);
		idt_nt_write(ndev, IDT_NT_LUTMDATA, 0);
		idt_nt_write(ndev, IDT_NT_LUTUDATA, 0);
		mmiowb();
		spin_unlock_irqrestore(&ndev->lut_lock, irqflags);
	}

	return 0;
}

/*=============================================================================
 *                          5. Doorbell operations
 *
 *    Doorbell functionality of IDT PCIe-switches is pretty unusual. First of
 * all there is global doorbell register which state can be changed by any
 * NT-function of the IDT device in accordance with global permissions. These
 * permissions configs are not supported by NTB API, so it must be done by
 * either BIOS or EEPROM settings. In the same way the state of the global
 * doorbell is reflected to the NT-functions local inbound doorbell registers.
 * It can lead to situations when client driver sets some peer doorbell bits
 * and get them bounced back to local inbound doorbell if permissions are
 * granted.
 *    Secondly there is just one IRQ vector for Doorbell, Message, Temperature
 * and Switch events, so if client driver left any of Doorbell bits set and
 * some other event occurred, the driver will be notified of Doorbell event
 * again.
 *=============================================================================
 */

/*
 * idt_db_isr() - doorbell event ISR
 * @ndev:	IDT NTB hardware driver descriptor
 * @ntint_sts:	NT-function interrupt status
 *
 * Doorbell event happans when DBELL bit of NTINTSTS switches from 0 to 1.
 * It happens only when unmasked doorbell bits are set to ones on completely
 * zeroed doorbell register.
 * The method is called from PCIe ISR bottom-half routine.
 */
static void idt_db_isr(struct idt_ntb_dev *ndev, u32 ntint_sts)
{
	/*
	 * Doorbell IRQ status will be cleaned only when client
	 * driver unsets all the doorbell bits.
	 */
	dev_dbg(&ndev->ntb.pdev->dev, "DB IRQ detected %#08x", ntint_sts);

	/* Notify the client driver of possible doorbell state change */
	ntb_db_event(&ndev->ntb, 0);
}

/*
 * idt_ntb_db_valid_mask() - get a mask of doorbell bits supported by the ntb
 *			     (NTB API callback)
 * @ntb:	NTB device context.
 *
 * IDT PCIe-switches expose just one Doorbell register of DWORD size.
 *
 * Return: A mask of doorbell bits supported by the ntb.
 */
static u64 idt_ntb_db_valid_mask(struct ntb_dev *ntb)
{
	return IDT_DBELL_MASK;
}

/*
 * idt_ntb_db_read() - read the local doorbell register (NTB API callback)
 * @ntb:	NTB device context.
 *
 * There is just on inbound doorbell register of each NT-function, so
 * this method return it value.
 *
 * Return: The bits currently set in the local doorbell register.
 */
static u64 idt_ntb_db_read(struct ntb_dev *ntb)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	return idt_nt_read(ndev, IDT_NT_INDBELLSTS);
}

/*
 * idt_ntb_db_clear() - clear bits in the local doorbell register
 *			(NTB API callback)
 * @ntb:	NTB device context.
 * @db_bits:	Doorbell bits to clear.
 *
 * Clear bits of inbound doorbell register by writing ones to it.
 *
 * NOTE! Invalid bits are always considered cleared so it's not an error
 * to clear them over.
 *
 * Return: always zero as success.
 */
static int idt_ntb_db_clear(struct ntb_dev *ntb, u64 db_bits)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	idt_nt_write(ndev, IDT_NT_INDBELLSTS, (u32)db_bits);

	return 0;
}

/*
 * idt_ntb_db_read_mask() - read the local doorbell mask (NTB API callback)
 * @ntb:	NTB device context.
 *
 * Each inbound doorbell bit can be masked from generating IRQ by setting
 * the corresponding bit in inbound doorbell mask. So this method returns
 * the value of the register.
 *
 * Return: The bits currently set in the local doorbell mask register.
 */
static u64 idt_ntb_db_read_mask(struct ntb_dev *ntb)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	return idt_nt_read(ndev, IDT_NT_INDBELLMSK);
}

/*
 * idt_ntb_db_set_mask() - set bits in the local doorbell mask
 *			   (NTB API callback)
 * @ntb:	NTB device context.
 * @db_bits:	Doorbell mask bits to set.
 *
 * The inbound doorbell register mask value must be read, then OR'ed with
 * passed field and only then set back.
 *
 * Return: zero on success, negative error if invalid argument passed.
 */
static int idt_ntb_db_set_mask(struct ntb_dev *ntb, u64 db_bits)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	return idt_reg_set_bits(ndev, IDT_NT_INDBELLMSK, &ndev->db_mask_lock,
				IDT_DBELL_MASK, db_bits);
}

/*
 * idt_ntb_db_clear_mask() - clear bits in the local doorbell mask
 *			     (NTB API callback)
 * @ntb:	NTB device context.
 * @db_bits:	Doorbell bits to clear.
 *
 * The method just clears the set bits up in accordance with the passed
 * bitfield. IDT PCIe-switch shall generate an interrupt if there hasn't
 * been any unmasked bit set before current unmasking. Otherwise IRQ won't
 * be generated since there is only one IRQ vector for all doorbells.
 *
 * Return: always zero as success
 */
static int idt_ntb_db_clear_mask(struct ntb_dev *ntb, u64 db_bits)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	idt_reg_clear_bits(ndev, IDT_NT_INDBELLMSK, &ndev->db_mask_lock,
			   db_bits);

	return 0;
}

/*
 * idt_ntb_peer_db_set() - set bits in the peer doorbell register
 *			   (NTB API callback)
 * @ntb:	NTB device context.
 * @db_bits:	Doorbell bits to set.
 *
 * IDT PCIe-switches exposes local outbound doorbell register to change peer
 * inbound doorbell register state.
 *
 * Return: zero on success, negative error if invalid argument passed.
 */
static int idt_ntb_peer_db_set(struct ntb_dev *ntb, u64 db_bits)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	if (db_bits & ~(u64)IDT_DBELL_MASK)
		return -EINVAL;

	idt_nt_write(ndev, IDT_NT_OUTDBELLSET, (u32)db_bits);
	return 0;
}

/*=============================================================================
 *                          6. Messaging operations
 *
 *    Each NT-function of IDT PCIe-switch has four inbound and four outbound
 * message registers. Each outbound message register can be connected to one or
 * even more than one peer inbound message registers by setting global
 * configurations. Since NTB API permits one-on-one message registers mapping
 * only, the driver acts in according with that restriction.
 *=============================================================================
 */

/*
 * idt_init_msg() - initialize messaging interface
 * @ndev:	IDT NTB hardware driver descriptor
 *
 * Just initialize the message registers routing tables locker.
 */
static void idt_init_msg(struct idt_ntb_dev *ndev)
{
	unsigned char midx;

	/* Init the messages routing table lockers */
	for (midx = 0; midx < IDT_MSG_CNT; midx++)
		spin_lock_init(&ndev->msg_locks[midx]);

	dev_dbg(&ndev->ntb.pdev->dev, "NTB Messaging initialized");
}

/*
 * idt_msg_isr() - message event ISR
 * @ndev:	IDT NTB hardware driver descriptor
 * @ntint_sts:	NT-function interrupt status
 *
 * Message event happens when MSG bit of NTINTSTS switches from 0 to 1.
 * It happens only when unmasked message status bits are set to ones on
 * completely zeroed message status register.
 * The method is called from PCIe ISR bottom-half routine.
 */
static void idt_msg_isr(struct idt_ntb_dev *ndev, u32 ntint_sts)
{
	/*
	 * Message IRQ status will be cleaned only when client
	 * driver unsets all the message status bits.
	 */
	dev_dbg(&ndev->ntb.pdev->dev, "Message IRQ detected %#08x", ntint_sts);

	/* Notify the client driver of possible message status change */
	ntb_msg_event(&ndev->ntb);
}

/*
 * idt_ntb_msg_count() - get the number of message registers (NTB API callback)
 * @ntb:	NTB device context.
 *
 * IDT PCIe-switches support four message registers.
 *
 * Return: the number of message registers.
 */
static int idt_ntb_msg_count(struct ntb_dev *ntb)
{
	return IDT_MSG_CNT;
}

/*
 * idt_ntb_msg_inbits() - get a bitfield of inbound message registers status
 *			  (NTB API callback)
 * @ntb:	NTB device context.
 *
 * NT message status register is shared between inbound and outbound message
 * registers status
 *
 * Return: bitfield of inbound message registers.
 */
static u64 idt_ntb_msg_inbits(struct ntb_dev *ntb)
{
	return (u64)IDT_INMSG_MASK;
}

/*
 * idt_ntb_msg_outbits() - get a bitfield of outbound message registers status
 *			  (NTB API callback)
 * @ntb:	NTB device context.
 *
 * NT message status register is shared between inbound and outbound message
 * registers status
 *
 * Return: bitfield of outbound message registers.
 */
static u64 idt_ntb_msg_outbits(struct ntb_dev *ntb)
{
	return (u64)IDT_OUTMSG_MASK;
}

/*
 * idt_ntb_msg_read_sts() - read the message registers status (NTB API callback)
 * @ntb:	NTB device context.
 *
 * IDT PCIe-switches expose message status registers to notify drivers of
 * incoming data and failures in case if peer message register isn't freed.
 *
 * Return: status bits of message registers
 */
static u64 idt_ntb_msg_read_sts(struct ntb_dev *ntb)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	return idt_nt_read(ndev, IDT_NT_MSGSTS);
}

/*
 * idt_ntb_msg_clear_sts() - clear status bits of message registers
 *			     (NTB API callback)
 * @ntb:	NTB device context.
 * @sts_bits:	Status bits to clear.
 *
 * Clear bits in the status register by writing ones.
 *
 * NOTE! Invalid bits are always considered cleared so it's not an error
 * to clear them over.
 *
 * Return: always zero as success.
 */
static int idt_ntb_msg_clear_sts(struct ntb_dev *ntb, u64 sts_bits)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	idt_nt_write(ndev, IDT_NT_MSGSTS, sts_bits);

	return 0;
}

/*
 * idt_ntb_msg_set_mask() - set mask of message register status bits
 *			    (NTB API callback)
 * @ntb:	NTB device context.
 * @mask_bits:	Mask bits.
 *
 * Mask the message status bits from raising an IRQ.
 *
 * Return: zero on success, negative error if invalid argument passed.
 */
static int idt_ntb_msg_set_mask(struct ntb_dev *ntb, u64 mask_bits)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	return idt_reg_set_bits(ndev, IDT_NT_MSGSTSMSK, &ndev->msg_mask_lock,
				IDT_MSG_MASK, mask_bits);
}

/*
 * idt_ntb_msg_clear_mask() - clear message registers mask
 *			      (NTB API callback)
 * @ntb:	NTB device context.
 * @mask_bits:	Mask bits.
 *
 * Clear mask of message status bits IRQs.
 *
 * Return: always zero as success.
 */
static int idt_ntb_msg_clear_mask(struct ntb_dev *ntb, u64 mask_bits)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	idt_reg_clear_bits(ndev, IDT_NT_MSGSTSMSK, &ndev->msg_mask_lock,
			   mask_bits);

	return 0;
}

/*
 * idt_ntb_msg_read() - read message register with specified index
 *			(NTB API callback)
 * @ntb:	NTB device context.
 * @pidx:	OUT - Port index of peer device a message retrieved from
 * @midx:	Message register index
 *
 * Read data from the specified message register and source register.
 *
 * Return: inbound message register value.
 */
static u32 idt_ntb_msg_read(struct ntb_dev *ntb, int *pidx, int midx)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);

	if (midx < 0 || IDT_MSG_CNT <= midx)
		return ~(u32)0;

	/* Retrieve source port index of the message */
	if (pidx != NULL) {
		u32 srcpart;

		srcpart = idt_nt_read(ndev, ntdata_tbl.msgs[midx].src);
		*pidx = ndev->part_idx_map[srcpart];

		/* Sanity check partition index (for initial case) */
		if (*pidx == -EINVAL)
			*pidx = 0;
	}

	/* Retrieve data of the corresponding message register */
	return idt_nt_read(ndev, ntdata_tbl.msgs[midx].in);
}

/*
 * idt_ntb_peer_msg_write() - write data to the specified message register
 *			      (NTB API callback)
 * @ntb:	NTB device context.
 * @pidx:	Port index of peer device a message being sent to
 * @midx:	Message register index
 * @msg:	Data to send
 *
 * Just try to send data to a peer. Message status register should be
 * checked by client driver.
 *
 * Return: zero on success, negative error if invalid argument passed.
 */
static int idt_ntb_peer_msg_write(struct ntb_dev *ntb, int pidx, int midx,
				  u32 msg)
{
	struct idt_ntb_dev *ndev = to_ndev_ntb(ntb);
	unsigned long irqflags;
	u32 swpmsgctl = 0;

	if (midx < 0 || IDT_MSG_CNT <= midx)
		return -EINVAL;

	if (pidx < 0 || ndev->peer_cnt <= pidx)
		return -EINVAL;

	/* Collect the routing information */
	swpmsgctl = SET_FIELD(SWPxMSGCTL_REG, 0, midx) |
		    SET_FIELD(SWPxMSGCTL_PART, 0, ndev->peers[pidx].part);

	/* Lock the messages routing table of the specified register */
	spin_lock_irqsave(&ndev->msg_locks[midx], irqflags);
	/* Set the route and send the data */
	idt_sw_write(ndev, partdata_tbl[ndev->part].msgctl[midx], swpmsgctl);
	idt_nt_write(ndev, ntdata_tbl.msgs[midx].out, msg);
	mmiowb();
	/* Unlock the messages routing table */
	spin_unlock_irqrestore(&ndev->msg_locks[midx], irqflags);

	/* Client driver shall check the status register */
	return 0;
}

/*=============================================================================
 *                      7. Temperature sensor operations
 *
 *    IDT PCIe-switch has an embedded temperature sensor, which can be used to
 * warn a user-space of possible chip overheating. Since workload temperature
 * can be different on different platforms, temperature thresholds as well as
 * general sensor settings must be setup in the framework of BIOS/EEPROM
 * initializations. It includes the actual sensor enabling as well.
 *=============================================================================
 */

/*
 * idt_read_temp() - read temperature from chip sensor
 * @ntb:	NTB device context.
 * @val:	OUT - integer value of temperature
 * @frac:	OUT - fraction
 */
static void idt_read_temp(struct idt_ntb_dev *ndev, unsigned char *val,
			  unsigned char *frac)
{
	u32 data;

	/* Read the data from TEMP field of the TMPSTS register */
	data = idt_sw_read(ndev, IDT_SW_TMPSTS);
	data = GET_FIELD(TMPSTS_TEMP, data);
	/* TEMP field has one fractional bit and seven integer bits */
	*val = data >> 1;
	*frac = ((data & 0x1) ? 5 : 0);
}

/*
 * idt_temp_isr() - temperature sensor alarm events ISR
 * @ndev:	IDT NTB hardware driver descriptor
 * @ntint_sts:	NT-function interrupt status
 *
 * It handles events of temperature crossing alarm thresholds. Since reading
 * of TMPALARM register clears it up, the function doesn't analyze the
 * read value, instead the current temperature value just warningly printed to
 * log.
 * The method is called from PCIe ISR bottom-half routine.
 */
static void idt_temp_isr(struct idt_ntb_dev *ndev, u32 ntint_sts)
{
	unsigned char val, frac;

	/* Read the current temperature value */
	idt_read_temp(ndev, &val, &frac);

	/* Read the temperature alarm to clean the alarm status out */
	/*(void)idt_sw_read(ndev, IDT_SW_TMPALARM);*/

	/* Clean the corresponding interrupt bit */
	idt_nt_write(ndev, IDT_NT_NTINTSTS, IDT_NTINTSTS_TMPSENSOR);

	dev_dbg(&ndev->ntb.pdev->dev,
		"Temp sensor IRQ detected %#08x", ntint_sts);

	/* Print temperature value to log */
	dev_warn(&ndev->ntb.pdev->dev, "Temperature %hhu.%hhu", val, frac);
}

/*=============================================================================
 *                           8. ISRs related operations
 *
 *    IDT PCIe-switch has strangely developed IRQ system. There is just one
 * interrupt vector for doorbell and message registers. So the hardware driver
 * can't determine actual source of IRQ if, for example, message event happened
 * while any of unmasked doorbell is still set. The similar situation may be if
 * switch or temperature sensor events pop up. The difference is that SEVENT
 * and TMPSENSOR bits of NT interrupt status register can be cleaned by
 * IRQ handler so a next interrupt request won't have false handling of
 * corresponding events.
 *    The hardware driver has only bottom-half handler of the IRQ, since if any
 * of events happened the device won't raise it again before the last one is
 * handled by clearing of corresponding NTINTSTS bit.
 *=============================================================================
 */

static irqreturn_t idt_thread_isr(int irq, void *devid);

/*
 * idt_init_isr() - initialize PCIe interrupt handler
 * @ndev:	IDT NTB hardware driver descriptor
 *
 * Return: zero on success, otherwise a negative error number.
 */
static int idt_init_isr(struct idt_ntb_dev *ndev)
{
	struct pci_dev *pdev = ndev->ntb.pdev;
	u32 ntint_mask;
	int ret;

	/* Allocate just one interrupt vector for the ISR */
	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI | PCI_IRQ_LEGACY);
	if (ret != 1) {
		dev_err(&pdev->dev, "Failed to allocate IRQ vector");
		return ret;
	}

	/* Retrieve the IRQ vector */
	ret = pci_irq_vector(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to get IRQ vector");
		goto err_free_vectors;
	}

	/* Set the IRQ handler */
	ret = devm_request_threaded_irq(&pdev->dev, ret, NULL, idt_thread_isr,
					IRQF_ONESHOT, NTB_IRQNAME, ndev);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to set MSI IRQ handler, %d", ret);
		goto err_free_vectors;
	}

	/* Unmask Message/Doorbell/SE/Temperature interrupts */
	ntint_mask = idt_nt_read(ndev, IDT_NT_NTINTMSK) & ~IDT_NTINTMSK_ALL;
	idt_nt_write(ndev, IDT_NT_NTINTMSK, ntint_mask);

	/* From now on the interrupts are enabled */
	dev_dbg(&pdev->dev, "NTB interrupts initialized");

	return 0;

err_free_vectors:
	pci_free_irq_vectors(pdev);

	return ret;
}


/*
 * idt_deinit_ist() - deinitialize PCIe interrupt handler
 * @ndev:	IDT NTB hardware driver descriptor
 *
 * Disable corresponding interrupts and free allocated IRQ vectors.
 */
static void idt_deinit_isr(struct idt_ntb_dev *ndev)
{
	struct pci_dev *pdev = ndev->ntb.pdev;
	u32 ntint_mask;

	/* Mask interrupts back */
	ntint_mask = idt_nt_read(ndev, IDT_NT_NTINTMSK) | IDT_NTINTMSK_ALL;
	idt_nt_write(ndev, IDT_NT_NTINTMSK, ntint_mask);

	/* Manually free IRQ otherwise PCI free irq vectors will fail */
	devm_free_irq(&pdev->dev, pci_irq_vector(pdev, 0), ndev);

	/* Free allocated IRQ vectors */
	pci_free_irq_vectors(pdev);

	dev_dbg(&pdev->dev, "NTB interrupts deinitialized");
}

/*
 * idt_thread_isr() - NT function interrupts handler
 * @irq:	IRQ number
 * @devid:	Custom buffer
 *
 * It reads current NT interrupts state register and handles all the event
 * it declares.
 * The method is bottom-half routine of actual default PCIe IRQ handler.
 */
static irqreturn_t idt_thread_isr(int irq, void *devid)
{
	struct idt_ntb_dev *ndev = devid;
	bool handled = false;
	u32 ntint_sts;

	/* Read the NT interrupts status register */
	ntint_sts = idt_nt_read(ndev, IDT_NT_NTINTSTS);

	/* Handle messaging interrupts */
	if (ntint_sts & IDT_NTINTSTS_MSG) {
		idt_msg_isr(ndev, ntint_sts);
		handled = true;
	}

	/* Handle doorbell interrupts */
	if (ntint_sts & IDT_NTINTSTS_DBELL) {
		idt_db_isr(ndev, ntint_sts);
		handled = true;
	}

	/* Handle switch event interrupts */
	if (ntint_sts & IDT_NTINTSTS_SEVENT) {
		idt_se_isr(ndev, ntint_sts);
		handled = true;
	}

	/* Handle temperature sensor interrupt */
	if (ntint_sts & IDT_NTINTSTS_TMPSENSOR) {
		idt_temp_isr(ndev, ntint_sts);
		handled = true;
	}

	dev_dbg(&ndev->ntb.pdev->dev, "IDT IRQs 0x%08x handled", ntint_sts);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

/*===========================================================================
 *                     9. NTB hardware driver initialization
 *===========================================================================
 */

/*
 * NTB API operations
 */
static const struct ntb_dev_ops idt_ntb_ops = {
	.port_number		= idt_ntb_port_number,
	.peer_port_count	= idt_ntb_peer_port_count,
	.peer_port_number	= idt_ntb_peer_port_number,
	.peer_port_idx		= idt_ntb_peer_port_idx,
	.link_is_up		= idt_ntb_link_is_up,
	.link_enable		= idt_ntb_link_enable,
	.link_disable		= idt_ntb_link_disable,
	.mw_count		= idt_ntb_mw_count,
	.mw_get_align		= idt_ntb_mw_get_align,
	.peer_mw_count		= idt_ntb_peer_mw_count,
	.peer_mw_get_addr	= idt_ntb_peer_mw_get_addr,
	.peer_mw_set_trans	= idt_ntb_peer_mw_set_trans,
	.peer_mw_clear_trans	= idt_ntb_peer_mw_clear_trans,
	.db_valid_mask		= idt_ntb_db_valid_mask,
	.db_read		= idt_ntb_db_read,
	.db_clear		= idt_ntb_db_clear,
	.db_read_mask		= idt_ntb_db_read_mask,
	.db_set_mask		= idt_ntb_db_set_mask,
	.db_clear_mask		= idt_ntb_db_clear_mask,
	.peer_db_set		= idt_ntb_peer_db_set,
	.msg_count		= idt_ntb_msg_count,
	.msg_inbits		= idt_ntb_msg_inbits,
	.msg_outbits		= idt_ntb_msg_outbits,
	.msg_read_sts		= idt_ntb_msg_read_sts,
	.msg_clear_sts		= idt_ntb_msg_clear_sts,
	.msg_set_mask		= idt_ntb_msg_set_mask,
	.msg_clear_mask		= idt_ntb_msg_clear_mask,
	.msg_read		= idt_ntb_msg_read,
	.peer_msg_write		= idt_ntb_peer_msg_write
};

/*
 * idt_register_device() - register IDT NTB device
 * @ndev:	IDT NTB hardware driver descriptor
 *
 * Return: zero on success, otherwise a negative error number.
 */
static int idt_register_device(struct idt_ntb_dev *ndev)
{
	int ret;

	/* Initialize the rest of NTB device structure and register it */
	ndev->ntb.ops = &idt_ntb_ops;
	ndev->ntb.topo = NTB_TOPO_SWITCH;

	ret = ntb_register_device(&ndev->ntb);
	if (ret != 0) {
		dev_err(&ndev->ntb.pdev->dev, "Failed to register NTB device");
		return ret;
	}

	dev_dbg(&ndev->ntb.pdev->dev, "NTB device successfully registered");

	return 0;
}

/*
 * idt_unregister_device() - unregister IDT NTB device
 * @ndev:	IDT NTB hardware driver descriptor
 */
static void idt_unregister_device(struct idt_ntb_dev *ndev)
{
	/* Just unregister the NTB device */
	ntb_unregister_device(&ndev->ntb);

	dev_dbg(&ndev->ntb.pdev->dev, "NTB device unregistered");
}

/*=============================================================================
 *                        10. DebugFS node initialization
 *=============================================================================
 */

static ssize_t idt_dbgfs_info_read(struct file *filp, char __user *ubuf,
				   size_t count, loff_t *offp);

/*
 * Driver DebugFS info file operations
 */
static const struct file_operations idt_dbgfs_info_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = idt_dbgfs_info_read
};

/*
 * idt_dbgfs_info_read() - DebugFS read info node callback
 * @file:	File node descriptor.
 * @ubuf:	User-space buffer to put data to
 * @count:	Size of the buffer
 * @offp:	Offset within the buffer
 */
static ssize_t idt_dbgfs_info_read(struct file *filp, char __user *ubuf,
				   size_t count, loff_t *offp)
{
	struct idt_ntb_dev *ndev = filp->private_data;
	unsigned char temp, frac, idx, pidx, cnt;
	ssize_t ret = 0, off = 0;
	unsigned long irqflags;
	enum ntb_speed speed;
	enum ntb_width width;
	char *strbuf;
	size_t size;
	u32 data;

	/* Lets limit the buffer size the way the Intel/AMD drivers do */
	size = min_t(size_t, count, 0x1000U);

	/* Allocate the memory for the buffer */
	strbuf = kmalloc(size, GFP_KERNEL);
	if (strbuf == NULL)
		return -ENOMEM;

	/* Put the data into the string buffer */
	off += scnprintf(strbuf + off, size - off,
		"\n\t\tIDT NTB device Information:\n\n");

	/* General local device configurations */
	off += scnprintf(strbuf + off, size - off,
		"Local Port %hhu, Partition %hhu\n", ndev->port, ndev->part);

	/* Peer ports information */
	off += scnprintf(strbuf + off, size - off, "Peers:\n");
	for (idx = 0; idx < ndev->peer_cnt; idx++) {
		off += scnprintf(strbuf + off, size - off,
			"\t%hhu. Port %hhu, Partition %hhu\n",
			idx, ndev->peers[idx].port, ndev->peers[idx].part);
	}

	/* Links status */
	data = idt_ntb_link_is_up(&ndev->ntb, &speed, &width);
	off += scnprintf(strbuf + off, size - off,
		"NTB link status\t- 0x%08x, ", data);
	off += scnprintf(strbuf + off, size - off, "PCIe Gen %d x%d lanes\n",
		speed, width);

	/* Mapping table entries */
	off += scnprintf(strbuf + off, size - off, "NTB Mapping Table:\n");
	for (idx = 0; idx < IDT_MTBL_ENTRY_CNT; idx++) {
		spin_lock_irqsave(&ndev->mtbl_lock, irqflags);
		idt_nt_write(ndev, IDT_NT_NTMTBLADDR, idx);
		data = idt_nt_read(ndev, IDT_NT_NTMTBLDATA);
		spin_unlock_irqrestore(&ndev->mtbl_lock, irqflags);

		/* Print valid entries only */
		if (data & IDT_NTMTBLDATA_VALID) {
			off += scnprintf(strbuf + off, size - off,
				"\t%hhu. Partition %d, Requester ID 0x%04x\n",
				idx, GET_FIELD(NTMTBLDATA_PART, data),
				GET_FIELD(NTMTBLDATA_REQID, data));
		}
	}
	off += scnprintf(strbuf + off, size - off, "\n");

	/* Outbound memory windows information */
	off += scnprintf(strbuf + off, size - off,
		"Outbound Memory Windows:\n");
	for (idx = 0; idx < ndev->mw_cnt; idx += cnt) {
		data = ndev->mws[idx].type;
		cnt = idt_get_mw_count(data);

		/* Print Memory Window information */
		if (data == IDT_MW_DIR)
			off += scnprintf(strbuf + off, size - off,
				"\t%hhu.\t", idx);
		else
			off += scnprintf(strbuf + off, size - off,
				"\t%hhu-%hhu.\t", idx, idx + cnt - 1);

		off += scnprintf(strbuf + off, size - off, "%s BAR%hhu, ",
			idt_get_mw_name(data), ndev->mws[idx].bar);

		off += scnprintf(strbuf + off, size - off,
			"Address align 0x%08llx, ", ndev->mws[idx].addr_align);

		off += scnprintf(strbuf + off, size - off,
			"Size align 0x%08llx, Size max %llu\n",
			ndev->mws[idx].size_align, ndev->mws[idx].size_max);
	}

	/* Inbound memory windows information */
	for (pidx = 0; pidx < ndev->peer_cnt; pidx++) {
		off += scnprintf(strbuf + off, size - off,
			"Inbound Memory Windows for peer %hhu (Port %hhu):\n",
			pidx, ndev->peers[pidx].port);

		/* Print Memory Windows information */
		for (idx = 0; idx < ndev->peers[pidx].mw_cnt; idx += cnt) {
			data = ndev->peers[pidx].mws[idx].type;
			cnt = idt_get_mw_count(data);

			if (data == IDT_MW_DIR)
				off += scnprintf(strbuf + off, size - off,
					"\t%hhu.\t", idx);
			else
				off += scnprintf(strbuf + off, size - off,
					"\t%hhu-%hhu.\t", idx, idx + cnt - 1);

			off += scnprintf(strbuf + off, size - off,
				"%s BAR%hhu, ", idt_get_mw_name(data),
				ndev->peers[pidx].mws[idx].bar);

			off += scnprintf(strbuf + off, size - off,
				"Address align 0x%08llx, ",
				ndev->peers[pidx].mws[idx].addr_align);

			off += scnprintf(strbuf + off, size - off,
				"Size align 0x%08llx, Size max %llu\n",
				ndev->peers[pidx].mws[idx].size_align,
				ndev->peers[pidx].mws[idx].size_max);
		}
	}
	off += scnprintf(strbuf + off, size - off, "\n");

	/* Doorbell information */
	data = idt_sw_read(ndev, IDT_SW_GDBELLSTS);
	off += scnprintf(strbuf + off, size - off,
		 "Global Doorbell state\t- 0x%08x\n", data);
	data = idt_ntb_db_read(&ndev->ntb);
	off += scnprintf(strbuf + off, size - off,
		 "Local  Doorbell state\t- 0x%08x\n", data);
	data = idt_nt_read(ndev, IDT_NT_INDBELLMSK);
	off += scnprintf(strbuf + off, size - off,
		 "Local  Doorbell mask\t- 0x%08x\n", data);
	off += scnprintf(strbuf + off, size - off, "\n");

	/* Messaging information */
	off += scnprintf(strbuf + off, size - off,
		 "Message event valid\t- 0x%08x\n", IDT_MSG_MASK);
	data = idt_ntb_msg_read_sts(&ndev->ntb);
	off += scnprintf(strbuf + off, size - off,
		 "Message event status\t- 0x%08x\n", data);
	data = idt_nt_read(ndev, IDT_NT_MSGSTSMSK);
	off += scnprintf(strbuf + off, size - off,
		 "Message event mask\t- 0x%08x\n", data);
	off += scnprintf(strbuf + off, size - off,
		 "Message data:\n");
	for (idx = 0; idx < IDT_MSG_CNT; idx++) {
		int src;
		data = idt_ntb_msg_read(&ndev->ntb, &src, idx);
		off += scnprintf(strbuf + off, size - off,
			"\t%hhu. 0x%08x from peer %hhu (Port %hhu)\n",
			idx, data, src, ndev->peers[src].port);
	}
	off += scnprintf(strbuf + off, size - off, "\n");

	/* Current temperature */
	idt_read_temp(ndev, &temp, &frac);
	off += scnprintf(strbuf + off, size - off,
		"Switch temperature\t\t- %hhu.%hhuC\n", temp, frac);

	/* Copy the buffer to the User Space */
	ret = simple_read_from_buffer(ubuf, count, offp, strbuf, off);
	kfree(strbuf);

	return ret;
}

/*
 * idt_init_dbgfs() - initialize DebugFS node
 * @ndev:	IDT NTB hardware driver descriptor
 *
 * Return: zero on success, otherwise a negative error number.
 */
static int idt_init_dbgfs(struct idt_ntb_dev *ndev)
{
	char devname[64];

	/* If the top directory is not created then do nothing */
	if (IS_ERR_OR_NULL(dbgfs_topdir)) {
		dev_info(&ndev->ntb.pdev->dev, "Top DebugFS directory absent");
		return PTR_ERR(dbgfs_topdir);
	}

	/* Create the info file node */
	snprintf(devname, 64, "info:%s", pci_name(ndev->ntb.pdev));
	ndev->dbgfs_info = debugfs_create_file(devname, 0400, dbgfs_topdir,
		ndev, &idt_dbgfs_info_ops);
	if (IS_ERR(ndev->dbgfs_info)) {
		dev_dbg(&ndev->ntb.pdev->dev, "Failed to create DebugFS node");
		return PTR_ERR(ndev->dbgfs_info);
	}

	dev_dbg(&ndev->ntb.pdev->dev, "NTB device DebugFS node created");

	return 0;
}

/*
 * idt_deinit_dbgfs() - deinitialize DebugFS node
 * @ndev:	IDT NTB hardware driver descriptor
 *
 * Just discard the info node from DebugFS
 */
static void idt_deinit_dbgfs(struct idt_ntb_dev *ndev)
{
	debugfs_remove(ndev->dbgfs_info);

	dev_dbg(&ndev->ntb.pdev->dev, "NTB device DebugFS node discarded");
}

/*=============================================================================
 *                     11. Basic PCIe device initialization
 *=============================================================================
 */

/*
 * idt_check_setup() - Check whether the IDT PCIe-swtich is properly
 *		       pre-initialized
 * @pdev:	Pointer to the PCI device descriptor
 *
 * Return: zero on success, otherwise a negative error number.
 */
static int idt_check_setup(struct pci_dev *pdev)
{
	u32 data;
	int ret;

	/* Read the BARSETUP0 */
	ret = pci_read_config_dword(pdev, IDT_NT_BARSETUP0, &data);
	if (ret != 0) {
		dev_err(&pdev->dev,
			"Failed to read BARSETUP0 config register");
		return ret;
	}

	/* Check whether the BAR0 register is enabled to be of config space */
	if (!(data & IDT_BARSETUP_EN) || !(data & IDT_BARSETUP_MODE_CFG)) {
		dev_err(&pdev->dev, "BAR0 doesn't map config space");
		return -EINVAL;
	}

	/* Configuration space BAR0 must have certain size */
	if ((data & IDT_BARSETUP_SIZE_MASK) != IDT_BARSETUP_SIZE_CFG) {
		dev_err(&pdev->dev, "Invalid size of config space");
		return -EINVAL;
	}

	dev_dbg(&pdev->dev, "NTB device pre-initialized correctly");

	return 0;
}

/*
 * Create the IDT PCIe-switch driver descriptor
 * @pdev:	Pointer to the PCI device descriptor
 * @id:		IDT PCIe-device configuration
 *
 * It just allocates a memory for IDT PCIe-switch device structure and
 * initializes some commonly used fields.
 *
 * No need of release method, since managed device resource is used for
 * memory allocation.
 *
 * Return: pointer to the descriptor, otherwise a negative error number.
 */
static struct idt_ntb_dev *idt_create_dev(struct pci_dev *pdev,
					  const struct pci_device_id *id)
{
	struct idt_ntb_dev *ndev;

	/* Allocate memory for the IDT PCIe-device descriptor */
	ndev = devm_kzalloc(&pdev->dev, sizeof(*ndev), GFP_KERNEL);
	if (!ndev) {
		dev_err(&pdev->dev, "Memory allocation failed for descriptor");
		return ERR_PTR(-ENOMEM);
	}

	/* Save the IDT PCIe-switch ports configuration */
	ndev->swcfg = (struct idt_89hpes_cfg *)id->driver_data;
	/* Save the PCI-device pointer inside the NTB device structure */
	ndev->ntb.pdev = pdev;

	/* Initialize spin locker of Doorbell, Message and GASA registers */
	spin_lock_init(&ndev->db_mask_lock);
	spin_lock_init(&ndev->msg_mask_lock);
	spin_lock_init(&ndev->gasa_lock);

	dev_info(&pdev->dev, "IDT %s discovered", ndev->swcfg->name);

	dev_dbg(&pdev->dev, "NTB device descriptor created");

	return ndev;
}

/*
 * idt_init_pci() - initialize the basic PCI-related subsystem
 * @ndev:	Pointer to the IDT PCIe-switch driver descriptor
 *
 * Managed device resources will be freed automatically in case of failure or
 * driver detachment.
 *
 * Return: zero on success, otherwise negative error number.
 */
static int idt_init_pci(struct idt_ntb_dev *ndev)
{
	struct pci_dev *pdev = ndev->ntb.pdev;
	int ret;

	/* Initialize the bit mask of PCI/NTB DMA */
	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (ret != 0) {
		ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (ret != 0) {
			dev_err(&pdev->dev, "Failed to set DMA bit mask\n");
			return ret;
		}
		dev_warn(&pdev->dev, "Cannot set DMA highmem bit mask\n");
	}
	ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (ret != 0) {
		ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (ret != 0) {
			dev_err(&pdev->dev,
				"Failed to set consistent DMA bit mask\n");
			return ret;
		}
		dev_warn(&pdev->dev,
			"Cannot set consistent DMA highmem bit mask\n");
	}
	ret = dma_coerce_mask_and_coherent(&ndev->ntb.dev,
					   dma_get_mask(&pdev->dev));
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to set NTB device DMA bit mask\n");
		return ret;
	}

	/*
	 * Enable the device advanced error reporting. It's not critical to
	 * have AER disabled in the kernel.
	 */
	ret = pci_enable_pcie_error_reporting(pdev);
	if (ret != 0)
		dev_warn(&pdev->dev, "PCIe AER capability disabled\n");
	else /* Cleanup uncorrectable error status before getting to init */
		pci_cleanup_aer_uncorrect_error_status(pdev);

	/* First enable the PCI device */
	ret = pcim_enable_device(pdev);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to enable PCIe device\n");
		goto err_disable_aer;
	}

	/*
	 * Enable the bus mastering, which effectively enables MSI IRQs and
	 * Request TLPs translation
	 */
	pci_set_master(pdev);

	/* Request all BARs resources and map BAR0 only */
	ret = pcim_iomap_regions_request_all(pdev, 1, NTB_NAME);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request resources\n");
		goto err_clear_master;
	}

	/* Retrieve virtual address of BAR0 - PCI configuration space */
	ndev->cfgspc = pcim_iomap_table(pdev)[0];

	/* Put the IDT driver data pointer to the PCI-device private pointer */
	pci_set_drvdata(pdev, ndev);

	dev_dbg(&pdev->dev, "NT-function PCIe interface initialized");

	return 0;

err_clear_master:
	pci_clear_master(pdev);
err_disable_aer:
	(void)pci_disable_pcie_error_reporting(pdev);

	return ret;
}

/*
 * idt_deinit_pci() - deinitialize the basic PCI-related subsystem
 * @ndev:	Pointer to the IDT PCIe-switch driver descriptor
 *
 * Managed resources will be freed on the driver detachment
 */
static void idt_deinit_pci(struct idt_ntb_dev *ndev)
{
	struct pci_dev *pdev = ndev->ntb.pdev;

	/* Clean up the PCI-device private data pointer */
	pci_set_drvdata(pdev, NULL);

	/* Clear the bus master disabling the Request TLPs translation */
	pci_clear_master(pdev);

	/* Disable the AER capability */
	(void)pci_disable_pcie_error_reporting(pdev);

	dev_dbg(&pdev->dev, "NT-function PCIe interface cleared");
}

/*===========================================================================
 *                       12. PCI bus callback functions
 *===========================================================================
 */

/*
 * idt_pci_probe() - PCI device probe callback
 * @pdev:	Pointer to PCI device structure
 * @id:		PCIe device custom descriptor
 *
 * Return: zero on success, otherwise negative error number
 */
static int idt_pci_probe(struct pci_dev *pdev,
			 const struct pci_device_id *id)
{
	struct idt_ntb_dev *ndev;
	int ret;

	/* Check whether IDT PCIe-switch is properly pre-initialized */
	ret = idt_check_setup(pdev);
	if (ret != 0)
		return ret;

	/* Allocate the memory for IDT NTB device data */
	ndev = idt_create_dev(pdev, id);
	if (IS_ERR_OR_NULL(ndev))
		return PTR_ERR(ndev);

	/* Initialize the basic PCI subsystem of the device */
	ret = idt_init_pci(ndev);
	if (ret != 0)
		return ret;

	/* Scan ports of the IDT PCIe-switch */
	(void)idt_scan_ports(ndev);

	/* Initialize NTB link events subsystem */
	idt_init_link(ndev);

	/* Initialize MWs subsystem */
	ret = idt_init_mws(ndev);
	if (ret != 0)
		goto err_deinit_link;

	/* Initialize Messaging subsystem */
	idt_init_msg(ndev);

	/* Initialize IDT interrupts handler */
	ret = idt_init_isr(ndev);
	if (ret != 0)
		goto err_deinit_link;

	/* Register IDT NTB devices on the NTB bus */
	ret = idt_register_device(ndev);
	if (ret != 0)
		goto err_deinit_isr;

	/* Initialize DebugFS info node */
	(void)idt_init_dbgfs(ndev);

	/* IDT PCIe-switch NTB driver is finally initialized */
	dev_info(&pdev->dev, "IDT NTB device is ready");

	/* May the force be with us... */
	return 0;

err_deinit_isr:
	idt_deinit_isr(ndev);
err_deinit_link:
	idt_deinit_link(ndev);
	idt_deinit_pci(ndev);

	return ret;
}

/*
 * idt_pci_probe() - PCI device remove callback
 * @pdev:	Pointer to PCI device structure
 */
static void idt_pci_remove(struct pci_dev *pdev)
{
	struct idt_ntb_dev *ndev = pci_get_drvdata(pdev);

	/* Deinit the DebugFS node */
	idt_deinit_dbgfs(ndev);

	/* Unregister NTB device */
	idt_unregister_device(ndev);

	/* Stop the interrupts handling */
	idt_deinit_isr(ndev);

	/* Deinitialize link event subsystem */
	idt_deinit_link(ndev);

	/* Deinit basic PCI subsystem */
	idt_deinit_pci(ndev);

	/* IDT PCIe-switch NTB driver is finally initialized */
	dev_info(&pdev->dev, "IDT NTB device is removed");

	/* Sayonara... */
}

/*
 * IDT PCIe-switch models ports configuration structures
 */
static const struct idt_89hpes_cfg idt_89hpes24nt6ag2_config = {
	.name = "89HPES24NT6AG2",
	.port_cnt = 6, .ports = {0, 2, 4, 6, 8, 12}
};
static const struct idt_89hpes_cfg idt_89hpes32nt8ag2_config = {
	.name = "89HPES32NT8AG2",
	.port_cnt = 8, .ports = {0, 2, 4, 6, 8, 12, 16, 20}
};
static const struct idt_89hpes_cfg idt_89hpes32nt8bg2_config = {
	.name = "89HPES32NT8BG2",
	.port_cnt = 8, .ports = {0, 2, 4, 6, 8, 12, 16, 20}
};
static const struct idt_89hpes_cfg idt_89hpes12nt12g2_config = {
	.name = "89HPES12NT12G2",
	.port_cnt = 3, .ports = {0, 8, 16}
};
static const struct idt_89hpes_cfg idt_89hpes16nt16g2_config = {
	.name = "89HPES16NT16G2",
	.port_cnt = 4, .ports = {0, 8, 12, 16}
};
static const struct idt_89hpes_cfg idt_89hpes24nt24g2_config = {
	.name = "89HPES24NT24G2",
	.port_cnt = 8, .ports = {0, 2, 4, 6, 8, 12, 16, 20}
};
static const struct idt_89hpes_cfg idt_89hpes32nt24ag2_config = {
	.name = "89HPES32NT24AG2",
	.port_cnt = 8, .ports = {0, 2, 4, 6, 8, 12, 16, 20}
};
static const struct idt_89hpes_cfg idt_89hpes32nt24bg2_config = {
	.name = "89HPES32NT24BG2",
	.port_cnt = 8, .ports = {0, 2, 4, 6, 8, 12, 16, 20}
};

/*
 * PCI-ids table of the supported IDT PCIe-switch devices
 */
static const struct pci_device_id idt_pci_tbl[] = {
	{IDT_PCI_DEVICE_IDS(89HPES24NT6AG2,  idt_89hpes24nt6ag2_config)},
	{IDT_PCI_DEVICE_IDS(89HPES32NT8AG2,  idt_89hpes32nt8ag2_config)},
	{IDT_PCI_DEVICE_IDS(89HPES32NT8BG2,  idt_89hpes32nt8bg2_config)},
	{IDT_PCI_DEVICE_IDS(89HPES12NT12G2,  idt_89hpes12nt12g2_config)},
	{IDT_PCI_DEVICE_IDS(89HPES16NT16G2,  idt_89hpes16nt16g2_config)},
	{IDT_PCI_DEVICE_IDS(89HPES24NT24G2,  idt_89hpes24nt24g2_config)},
	{IDT_PCI_DEVICE_IDS(89HPES32NT24AG2, idt_89hpes32nt24ag2_config)},
	{IDT_PCI_DEVICE_IDS(89HPES32NT24BG2, idt_89hpes32nt24bg2_config)},
	{0}
};
MODULE_DEVICE_TABLE(pci, idt_pci_tbl);

/*
 * IDT PCIe-switch NT-function device driver structure definition
 */
static struct pci_driver idt_pci_driver = {
	.name		= KBUILD_MODNAME,
	.probe		= idt_pci_probe,
	.remove		= idt_pci_remove,
	.id_table	= idt_pci_tbl,
};

static int __init idt_pci_driver_init(void)
{
	pr_info("%s %s\n", NTB_DESC, NTB_VER);

	/* Create the top DebugFS directory if the FS is initialized */
	if (debugfs_initialized())
		dbgfs_topdir = debugfs_create_dir(KBUILD_MODNAME, NULL);

	/* Register the NTB hardware driver to handle the PCI device */
	return pci_register_driver(&idt_pci_driver);
}
module_init(idt_pci_driver_init);

static void __exit idt_pci_driver_exit(void)
{
	/* Unregister the NTB hardware driver */
	pci_unregister_driver(&idt_pci_driver);

	/* Discard the top DebugFS directory */
	debugfs_remove_recursive(dbgfs_topdir);
}
module_exit(idt_pci_driver_exit);

