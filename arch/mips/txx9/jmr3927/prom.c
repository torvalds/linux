/*
 * BRIEF MODULE DESCRIPTION
 *    PROM library initialisation code, assuming a version of
 *    pmon is the boot code.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *              ahennessy@mvista.com
 *
 * Based on arch/mips/au1000/common/prom.c
 *
 * This file was derived from Carsten Langgaard's
 * arch/mips/mips-boards/xx files.
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/bootinfo.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/jmr3927.h>

void __init jmr3927_prom_init(void)
{
	/* CCFG */
	if ((tx3927_ccfgptr->ccfg & TX3927_CCFG_TLBOFF) == 0)
		pr_err("TX3927 TLB off\n");

	add_memory_region(0, JMR3927_SDRAM_SIZE, BOOT_MEM_RAM);
	txx9_sio_putchar_init(TX3927_SIO_REG(1));
}
