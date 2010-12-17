
/*
 * arch/arm/mach-rk29/include/mach/rk29_nand.h
 *
 * Copyright (C) 2010 RockChip, Inc.
 * Author: 
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
 
#ifndef __ASM_ARCH_RK29_NAND_H
#define __ASM_ARCH_RK29_NAND_H


//BCHCTL¼Ä´æÆ÷
#define     BCH_WR                  0x0002
#define     BCH_RST                 0x0001
//FLCTL¼Ä´æÆ÷
#define     FL_RDY                  	(0x1<<20)
#define     FL_LBA_EN		     	(0x1<<11)
#define     FL_COR_EN             	(0x1<<10)
#define     FL_INT_EN             	(0x1<<9)
#define     FL_INTCLR        	     	(0x1<<8)
#define     FL_STMOD		     	(0x1<<7)
#define     FL_TRCNT			(0x3<<5)
#define     FL_STADDR		     	(0x1<<4)
#define     FL_BYPASS             	(0x1<<3)
#define     FL_START               	(0x1<<2)
#define     FL_RDN                   	(0x1<<1)
#define     FL_RST                    	(0x1<<0)
//FMCTL¼Ä´æÆ÷
#define     FMC_WP                    	(0x1<<8)
#define     FMC_FRDY		      		(0x1<<9)
#define     FMC_FRDY_INT_EN   	(0x1<<10)
#define     FMC_FRDY_INT_CLR		(0x1<<11)
#define     FMC_WIDTH_16				(0x1<<12)
//FMWAIT¼Ä´æÆ÷
#define     FMW_RWCS_OFFSET		0
#define     FMW_RWPW_OFFSET	5
#define     FMW_RDY                		 (0x1<<11)
#define     FMW_CSRW_OFFSET		12
#define     FMW_DLY_OFFSET		24//16

struct rk29_nand_timing {
	unsigned int	tCH;  /* Enable signal hold time */
	unsigned int	tCS;  /* Enable signal setup time */
	unsigned int	tWH;  /* ND_nWE high duration */
	unsigned int	tWP;  /* ND_nWE pulse time */
	unsigned int	tRH;  /* ND_nRE high duration */
	unsigned int	tRP;  /* ND_nRE pulse width */
	unsigned int	tR;   /* ND_nWE high to ND_nRE low for read */
	unsigned int	tWHR; /* ND_nWE high to ND_nRE low for status read */
	unsigned int	tAR;  /* ND_ALE low to ND_nRE low delay */
};

struct rk29_nand_cmdset {
	uint16_t	read1;
	uint16_t	read2;
	uint16_t	program;
	uint16_t	read_status;
	uint16_t	read_id;
	uint16_t	erase;
	uint16_t	reset;
	uint16_t	lock;
	uint16_t	unlock;
	uint16_t	lock_status;
};

typedef volatile struct tagCHIP_IF
{
	    uint32_t data;
	    uint32_t addr;
	    uint32_t cmd;
	    uint32_t RESERVED[0x3d];
}CHIP_IF, *pCHIP_IF;

//NANDC Registers
typedef  volatile struct tagNANDC
{
       volatile  uint32_t FMCTL; 
       volatile  uint32_t FMWAIT;
       volatile  uint32_t FLCTL;
       volatile  uint32_t BCHCTL; 
       volatile  uint32_t MTRANS_CFG;
       volatile  uint32_t MTRANS_SADDR0;
       volatile  uint32_t MTRANS_SADDR1;
       volatile  uint32_t MTRANS_STAT;
        
       volatile  uint32_t BCHST[8]; 
       volatile  uint32_t FLR1[(0x160-0x40)/4]; 
       volatile  uint32_t NANDC_VER;
       volatile  uint32_t FLR2[(0x200-0x164)/4]; 
       volatile  uint32_t spare[0x200/4]; 
       volatile  uint32_t RESERVED2[0x400/4]; 
       volatile  CHIP_IF chip[8];
       volatile  uint32_t buf[0x800/4]; 
}NANDC, *pNANDC;


struct rk29_nand_flash {
	const struct rk29_nand_timing *timing;        /* NAND Flash timing */
	const struct rk29_nand_cmdset *cmdset;

	uint32_t page_per_block;		/* Pages per block (PG_PER_BLK) */
	uint32_t page_size;				/* Page size in bytes (PAGE_SZ) */
	uint32_t flash_width;			/* Width of Flash memory (DWIDTH_M) */
	uint32_t num_blocks;			/* Number of physical blocks in Flash */
	uint32_t chip_id;
};

struct rk29_nand_platform_data {
	
	int width;			/* data bus width in bytes */
	int hw_ecc;			/* 1:hw ecc,    0: soft ecc */
	struct mtd_partition *parts;
	unsigned int	nr_parts;
       size_t		num_flash;
    int (*io_init)(void);
    int (*io_deinit)(void);
};


#endif /* __ASM_ARCH_RK29_NAND_H */

