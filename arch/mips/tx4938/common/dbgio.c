/*
 * linux/arch/mips/tx4938/common/dbgio.c
 *
 * kgdb interface for gdb
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * Copyright 2005 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Support for TX4938 in 2.6 - Hiroshi DOYU <Hiroshi_DOYU@montavista.co.jp>
 */

#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/tx4938/tx4938_mips.h>

extern u8 txx9_sio_kdbg_rd(void);
extern int txx9_sio_kdbg_wr( u8 ch );

u8 getDebugChar(void)
{
	return (txx9_sio_kdbg_rd());
}

int putDebugChar(u8 byte)
{
	return (txx9_sio_kdbg_wr(byte));
}

