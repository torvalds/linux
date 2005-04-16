#
# arch/v850/kernel/Makefile
#
#  Copyright (C) 2001,02,03  NEC Electronics Corporation
#  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

extra-y := head.o init_task.o vmlinux.lds

obj-y += intv.o entry.o process.o syscalls.o time.o semaphore.o setup.o \
	 signal.o irq.o mach.o ptrace.o bug.o
obj-$(CONFIG_MODULES)		+= module.o v850_ksyms.o
# chip-specific code
obj-$(CONFIG_V850E_MA1)		+= ma.o
obj-$(CONFIG_V850E_ME2)		+= me2.o
obj-$(CONFIG_V850E_TEG)		+= teg.o
obj-$(CONFIG_V850E_AS85EP1)	+= as85ep1.o
obj-$(CONFIG_V850E2_ANNA)	+= anna.o
# platform-specific code
obj-$(CONFIG_V850E_SIM)		+= sim.o simcons.o
obj-$(CONFIG_V850E2_SIM85E2)	+= sim85e2.o memcons.o
obj-$(CONFIG_V850E2_FPGA85E2C)	+= fpga85e2c.o memcons.o
obj-$(CONFIG_RTE_CB)		+= rte_cb.o rte_cb_leds.o
obj-$(CONFIG_RTE_CB_MA1)	+= rte_ma1_cb.o
obj-$(CONFIG_RTE_CB_ME2)	+= rte_me2_cb.o
obj-$(CONFIG_RTE_CB_NB85E)	+= rte_nb85e_cb.o
obj-$(CONFIG_RTE_CB_MULTI)	+= rte_cb_multi.o
obj-$(CONFIG_RTE_MB_A_PCI)	+= rte_mb_a_pci.o
obj-$(CONFIG_RTE_GBUS_INT)	+= gbus_int.o
# feature-specific code
obj-$(CONFIG_V850E_INTC)	+= v850e_intc.o
obj-$(CONFIG_V850E_TIMER_D)	+= v850e_timer_d.o v850e_utils.o
obj-$(CONFIG_V850E_CACHE)	+= v850e_cache.o
obj-$(CONFIG_V850E2_CACHE)	+= v850e2_cache.o
obj-$(CONFIG_V850E_HIGHRES_TIMER) += highres_timer.o
obj-$(CONFIG_PROC_FS)		+= procfs.o
