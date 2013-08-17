/*************************************************************************/ /*!
@Title          PDP common declarations
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.
 
This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS 
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING 
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR 
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN 
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
  
*/ /**************************************************************************/

#if !defined (__PDPCOMMON_H__)
#define __PDPCOMMON_H__

/* PowerVR/ImgTec device vendor identifier */
#define PVRPDP_VENDOR_ID_POWERVR			0x1010

/* PCI/PCIe device IDs */
#define PVRPDP_DEVICE_ID_PCI_EMULATOR		0x1CE0
#define PVRPDP_DEVICE_ID_PCI_FPGA			0x1CF0
#define PVRPDP_DEVICE_ID_PCI_ATLAS2_FPGA	0x1CF1
#define PVRPDP_DEVICE_ID_PCIE_ATLAS2_FPGA	0x1CF2
#define PVRPDP_DEVICE_ID_PCIE_EMULATOR		0x1CE3
#define PVRPDP_DEVICE_ID_PCIE_FPGA			0x1CF3

#if !defined(__linux__)
/* PCI/PCIe config space structure */
typedef struct
{
	union
	{
		unsigned char   aui8PCISpace[256];
		unsigned short  aui16PCISpace[128];
		unsigned long   aui32PCISpace[64];

		struct
		{
			unsigned short  ui16VenID;
			unsigned short  ui16DevID;
			unsigned short  ui16PCICmd;
			unsigned short  ui16PCIStatus;
		}s;
	}u;
	
	IMG_UINT32 aui32BAROffset[6];
	IMG_UINT32 aui32BARSize[6];
} PCICONFIG_SPACE, *PPCICONFIG_SPACE;
#endif

/* PCI register & memory defines.. */
#define PCI_BASEREG_OFFSET_DWORDS		4

#if 0 //ATLAS_REV == 2
	/* Atlas reg on base register 0 */
	#define PVRPDP_REG_PCI_BASENUM		0
	#define PVRPDP_REG_PCI_OFFSET		(PVRPDP_REG_PCI_BASENUM + PCI_BASEREG_OFFSET_DWORDS)
	
	#define PVRPDP_PCI_REG_OFFSET		0xC000
	#define PVRPDP_REG_SIZE				0x4000
	
	/* PDP mem (including HP mapping) on base register 2 */
	#define PVRPDP_MEM_PCI_BASENUM		2
	#define PVRPDP_MEM_PCI_OFFSET		(PVRPDP_MEM_PCI_BASENUM + PCI_BASEREG_OFFSET_DWORDS)
#else
	#if defined(EMULATE_ATLAS_3BAR)
		/* Atlas reg on base register 0 */
		#define PVRPDP_ATLAS_REG_PCI_BASENUM		    0
		#define PVRPDP_ATLAS_REG_PCI_OFFSET		    (PVRPDP_ATLAS_REG_PCI_BASENUM + PCI_BASEREG_OFFSET_DWORDS)
		
		#define PVRPDP_ATLAS_REG_OFFSET                0x0
		#define PVRPDP_ATLAS_REG_SIZE                  0x4000
		
		
		#define PVRPDP_ATLAS_REG_REGION_SIZE			0x10000
		
		/* SGX reg on base register 1 */
		#define PVRPDP_SGX_REG_PCI_BASENUM				1
		#define PVRPDP_SGX_REG_PCI_OFFSET		        (PVRPDP_SGX_REG_PCI_BASENUM + PCI_BASEREG_OFFSET_DWORDS)
		
		#define PVRPDP_SGX_REG_OFFSET                  0x0
		#define PVRPDP_SGX_REG_SIZE 				    0x4000
		
		#define PVRPDP_SGX_REG_REGION_SIZE				0x8000
		
		/* SGX mem (including HP mapping) on base register 2 */
		#define PVRPDP_SGX_MEM_PCI_BASENUM				2
		#define PVRPDP_SGX_MEM_PCI_OFFSET		        (PVRPDP_SGX_MEM_PCI_BASENUM + PCI_BASEREG_OFFSET_DWORDS)
		
		#define PVRPDP_SGX_MEM_REGION_SIZE				0x20000000
	#else  /* EMULATE_ATLAS_3BAR */
		/* Atlas Reg, SGX REG/SP, SGX mem (including HP mapping) on base register 2 */
		#define PVRPDP_PCI_BASENUM			2
		#define PVRPDP_PCI_OFFSET			(PVRPDP_PCI_BASENUM + PCI_BASEREG_OFFSET_DWORDS)
		
		#define PVRPDP_PCI_REG_OFFSET		0xC000
		#define PVRPDP_REG_SIZE				0x4000
		#define PVRPDP_PCI_MEM_OFFSET		0x10000
	#endif  /* EMULATE_ATLAS_3BAR */
#endif

/* Emulator SocIF defines.. */
#define PVRPDP_EMULATOR_PCI_SOC_OFFSET	0x9400
#define PVRPDP_EMULATOR_SOC_SIZE 		0x0100

/* TCF defines.. */
#define TCF_PCI_REG_OFFSET				0x0000
#define TCF_REG_SIZE					0x4000

/*
	PDP display surface offset..
	WARNING: It must be after services localmem region to avoid conflicts.
*/
	/* Give ourselves 32mb to play around in. */
	#define PVRPDP_SYSSURFACE_SIZE			(32 * 1024 * 1024)
	
	// PCI card has 256mb limit.
	#define PVRPDP_SYSSURFACE_OFFSET		((256 * 1024 * 1024) - PVRPDP_SYSSURFACE_SIZE)
	// PCIe card has 512mb limit.
	#define PVRPDP_PCIE_SYSSURFACE_OFFSET	((512 * 1024 * 1024) - PVRPDP_SYSSURFACE_SIZE)

#endif /* __PDPCOMMON_H__ */

/*****************************************************************************
 End of file (pdpcommon.h)
*****************************************************************************/
