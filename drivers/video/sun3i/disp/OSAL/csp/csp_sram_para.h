/*
 * drivers/video/sun3i/disp/OSAL/csp/csp_sram_para.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */


#ifndef	_CSP_SRAM_PARA_H_
#define	_CSP_SRAM_PARA_H_


//------ZONE--------------------------------------
typedef	enum{
	CSP_SRAM_ZONE_NULL		=	0x00,
	CSP_SRAM_ZONE_A1		        ,
	CSP_SRAM_ZONE_A2		        ,
	CSP_SRAM_ZONE_A3		        ,

	CSP_SRAM_ZONE_C1		        ,
	CSP_SRAM_ZONE_C2		        ,

	CSP_SRAM_ZONE_D1		        ,
	CSP_SRAM_ZONE_D2		        ,
	CSP_SRAM_ZONE_D3		        ,

	CSP_SRAM_ZONE_NFC		        ,
	CSP_SRAM_ZONE_ITCM		        ,
	CSP_SRAM_ZONE_ICACHE	        ,
	CSP_SRAM_ZONE_DCACHE			,

	CSP_SRAM_ZONE_MAX_NR
}csp_sram_zone_id_t;

//------Module--------------------------------------
typedef enum{
	CSP_SRAM_MODULE_NULL	=	0x00,
	CSP_SRAM_MODULE_CPU_DMA	        ,
	CSP_SRAM_MODULE_VE		        ,
	CSP_SRAM_MODULE_SIE0	        ,
	CSP_SRAM_MODULE_SIE1	        ,
	CSP_SRAM_MODULE_SIE2	        ,
	CSP_SRAM_MODULE_ACE		        ,
	CSP_SRAM_MODULE_EMAC			,

	CSP_SRAM_MODULE_MAX_NR
}csp_sram_module_t;


typedef struct  sram_zone_info{
	csp_sram_zone_id_t zone_id;
	u32 reserved;	//u32 zone_size;
}sram_zone_info_t;

typedef enum cpu_perf_cntr
{
    CSP_SRAM_PERF_CLKCNTR = 0,
    CSP_SRAM_PERF_INSTCNTR,
    CSP_SRAM_PERF_SWAITCNTR,
    CSP_SRAM_PERF_ICACNTR,
    CSP_SRAM_PERF_ICH1CNTR,
    CSP_SRAM_PERF_ICH2CNTR,
    CSP_SRAM_PERF_ICHCNTR,
    CSP_SRAM_PERF_ICWCNTR,
    CSP_SRAM_PERF_DCRACNTR,
    CSP_SRAM_PERF_DCRH1CNTR,
    CSP_SRAM_PERF_DCRH2CNTR,
    CSP_SRAM_PERF_DCRHCNTR,
    CSP_SRAM_PERF_DCRWCNTR,
    CSP_SRAM_PERF_DCWACNTR,
    CSP_SRAM_PERF_DCWH1CNTR,
    CSP_SRAM_PERF_DCWH2CNTR,
    CSP_SRAM_PERF_DCWHCNTR,
    CSP_SRAM_PERF_DCWWCNTR,
    CSP_SRAM_PERF_ICCCNTR,
    CSP_SRAM_PERF_

} cpu_perf_cntr_e;


#endif	//_CSP_SRAM_OPS_H_

