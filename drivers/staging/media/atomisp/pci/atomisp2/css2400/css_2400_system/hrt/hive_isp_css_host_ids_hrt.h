/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _hive_isp_css_host_ids_hrt_h_
#define _hive_isp_css_host_ids_hrt_h_

/* ISP_CSS identifiers */
#define INP_SYS       testbench_isp_inp_sys
#define ISYS_GP_REGS  testbench_isp_inp_sys_gpreg
#define ISYS_IRQ_CTRL testbench_isp_inp_sys_irq_ctrl
#define ISYS_CAP_A    testbench_isp_inp_sys_capt_unit_a
#define ISYS_CAP_B    testbench_isp_inp_sys_capt_unit_b
#define ISYS_CAP_C    testbench_isp_inp_sys_capt_unit_c
#define ISYS_INP_BUF  testbench_isp_inp_sys_input_buffer
#define ISYS_INP_CTRL testbench_isp_inp_sys_inp_ctrl
#define ISYS_ACQ      testbench_isp_inp_sys_acq_unit

#define ISP           testbench_isp_isp
#define SP            testbench_isp_scp

#define IF_PRIM       testbench_isp_ifmt_ift_prim  
#define IF_PRIM_B     testbench_isp_ifmt_ift_prim_b
#define IF_SEC        testbench_isp_ifmt_ift_sec
#define IF_SEC_MASTER testbench_isp_ifmt_ift_sec_mt_out
#define STR_TO_MEM    testbench_isp_ifmt_mem_cpy
#define IFMT_GP_REGS  testbench_isp_ifmt_gp_reg
#define IFMT_IRQ_CTRL testbench_isp_ifmt_irq_ctrl

#define CSS_RECEIVER  testbench_isp_inp_sys_csi_receiver

#define TC            testbench_isp_gpd_tc
#define GPTIMER       testbench_isp_gpd_gptimer
#define DMA           testbench_isp_isp_dma
#define GDC           testbench_isp_gdc1
#define GDC2          testbench_isp_gdc2
#define IRQ_CTRL      testbench_isp_gpd_irq_ctrl
#define GPIO          testbench_isp_gpd_c_gpio
#define GP_REGS       testbench_isp_gpd_gp_reg
#define ISEL_GP_REGS  testbench_isp_isel_gpr
#define ISEL_IRQ_CTRL testbench_isp_isel_irq_ctrl
#define DATA_MMU      testbench_isp_data_out_sys_c_mmu
#define ICACHE_MMU    testbench_isp_icache_out_sys_c_mmu

/* next is actually not FIFO but FIFO adapter, or slave to streaming adapter */
#define ISP_SP_FIFO   testbench_isp_fa_sp_isp
#define ISEL_FIFO     testbench_isp_isel_sf_fa_in

#define FIFO_GPF_SP   testbench_isp_sf_fa2sp_in
#define FIFO_GPF_ISP  testbench_isp_sf_fa2isp_in
#define FIFO_SP_GPF   testbench_isp_sf_sp2fa_in
#define FIFO_ISP_GPF  testbench_isp_sf_isp2fa_in

#define DATA_OCP_MASTER    testbench_isp_data_out_sys_cio2ocp_wide_data_out_mt
#define ICACHE_OCP_MASTER  testbench_isp_icache_out_sys_cio2ocp_wide_data_out_mt

#define SP_IN_FIFO    testbench_isp_sf_fa2sp_in
#define SP_OUT_FIFO   testbench_isp_sf_sp2fa_out
#define ISP_IN_FIFO   testbench_isp_sf_fa2isp_in
#define ISP_OUT_FIFO  testbench_isp_sf_isp2fa_out
#define GEN_SHORT_PACK_PORT testbench_isp_inp_sys_csi_str_mon_fa_gensh_out
#define ISYS_GP_REGS  testbench_isp_inp_sys_gpreg

/* Testbench identifiers */
#define DDR             testbench_ddram
#define DDR_SMALL       testbench_ddram_small
#define XMEM            DDR
#define GPIO_ADAPTER    testbench_gp_adapter
#define SIG_MONITOR     testbench_sig_mon
#define DDR_SLAVE       testbench_ddram_ip0
#define DDR_SMALL_SLAVE testbench_ddram_small_ip0
#define HOST_MASTER     host_op0

#endif /* _hive_isp_css_host_ids_hrt_h_ */
