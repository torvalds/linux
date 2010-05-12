/*
 * Copyright (c) 2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef PROJECT_AP15_RELOCATION_TABLE_SPEC_INC
#define PROJECT_AP15_RELOCATION_TABLE_SPEC_INC

// ------------------------------------------------------------
//    hw nvdevids
// ------------------------------------------------------------
// Memory Aperture: Internal Memory
#define NV_DEVID_IMEM                            1

// Memory Aperture: External Memory
#define NV_DEVID_EMEM                            2

// Memory Aperture: TCRAM
#define NV_DEVID_TCRAM                           3

// Memory Aperture: IRAM
#define NV_DEVID_IRAM                            4

// Memory Aperture: NOR FLASH
#define NV_DEVID_NOR                             5

// Memory Aperture: EXIO
#define NV_DEVID_EXIO                            6

// Memory Aperture: GART
#define NV_DEVID_GART                            7

// Device Aperture: Graphics Host (HOST1X)
#define NV_DEVID_HOST1X                          8

// Device Aperture: ARM PERIPH registers
#define NV_DEVID_ARM_PERIPH                      9

// Device Aperture: MSELECT
#define NV_DEVID_MSELECT                         10

// Device Aperture: memory controller
#define NV_DEVID_MC                              11

// Device Aperture: external memory controller
#define NV_DEVID_EMC                             12

// Device Aperture: video input
#define NV_DEVID_VI                              13

// Device Aperture: encoder pre-processor
#define NV_DEVID_EPP                             14

// Device Aperture: video encoder
#define NV_DEVID_MPE                             15

// Device Aperture: 3D engine
#define NV_DEVID_GR3D                            16

// Device Aperture: 2D + SBLT engine
#define NV_DEVID_GR2D                            17

// Device Aperture: Image Signal Processor
#define NV_DEVID_ISP                             18

// Device Aperture: DISPLAY
#define NV_DEVID_DISPLAY                         19

// Device Aperture: UPTAG
#define NV_DEVID_UPTAG                           20

// Device Aperture - SHR_SEM
#define NV_DEVID_SHR_SEM                         21

// Device Aperture - ARB_SEM
#define NV_DEVID_ARB_SEM                         22

// Device Aperture - ARB_PRI
#define NV_DEVID_ARB_PRI                         23

// Obsoleted for AP15
#define NV_DEVID_PRI_INTR                        24

// Obsoleted for AP15
#define NV_DEVID_SEC_INTR                        25

// Device Aperture: Timer Programmable
#define NV_DEVID_TMR                             26

// Device Aperture: Clock and Reset
#define NV_DEVID_CAR                             27

// Device Aperture: Flow control
#define NV_DEVID_FLOW                            28

// Device Aperture: Event
#define NV_DEVID_EVENT                           29

// Device Aperture: AHB DMA
#define NV_DEVID_AHB_DMA                         30

// Device Aperture: APB DMA
#define NV_DEVID_APB_DMA                         31

// Device Aperture: COP Cache Controller
#define NV_DEVID_COP_CACHE                       32

// Obsolete - use COP_CACHE
#define NV_DEVID_CC                              32

// Obsolete - use COP_CACHE
#define NV_DEVID_SYS_REG                         32

// Device Aperture: System Statistic monitor
#define NV_DEVID_STAT                            33

// Device Aperture: GPIO
#define NV_DEVID_GPIO                            34

// Device Aperture: Vector Co-Processor 2
#define NV_DEVID_VCP                             35

// Device Aperture: Arm Vectors
#define NV_DEVID_VECTOR                          36

// Device: MEM
#define NV_DEVID_MEM                             37

// Obsolete - use VDE
#define NV_DEVID_SXE                             38

// Device Aperture: video decoder
#define NV_DEVID_VDE                             38

// Obsolete - use VDE
#define NV_DEVID_BSEV                            39

// Obsolete - use VDE
#define NV_DEVID_MBE                             40

// Obsolete - use VDE
#define NV_DEVID_PPE                             41

// Obsolete - use VDE
#define NV_DEVID_MCE                             42

// Obsolete - use VDE
#define NV_DEVID_TFE                             43

// Obsolete - use VDE
#define NV_DEVID_PPB                             44

// Obsolete - use VDE
#define NV_DEVID_VDMA                            45

// Obsolete - use VDE
#define NV_DEVID_UCQ                             46

// Device Aperture: BSEA (now in AVP cluster)
#define NV_DEVID_BSEA                            47

// Obsolete - use VDE
#define NV_DEVID_FRAMEID                         48

// Device Aperture: Misc regs
#define NV_DEVID_MISC                            49

// Device Aperture: AC97
#define NV_DEVID_AC97                            50

// Device Aperture: S/P-DIF
#define NV_DEVID_SPDIF                           51

// Device Aperture: I2S
#define NV_DEVID_I2S                             52

// Device Aperture: UART
#define NV_DEVID_UART                            53

// Device Aperture: VFIR
#define NV_DEVID_VFIR                            54

// Device Aperture: NAND Flash Controller
#define NV_DEVID_NANDCTRL                        55

// Obsolete - use NANDCTRL
#define NV_DEVID_NANDFLASH                       55

// Device Aperture: HSMMC
#define NV_DEVID_HSMMC                           56

// Device Aperture: XIO
#define NV_DEVID_XIO                             57

// Device Aperture: PWFM
#define NV_DEVID_PWFM                            58

// Device Aperture: MIPI
#define NV_DEVID_MIPI_HS                         59

// Device Aperture: I2C
#define NV_DEVID_I2C                             60

// Device Aperture: TWC
#define NV_DEVID_TWC                             61

// Device Aperture: SLINK
#define NV_DEVID_SLINK                           62

// Device Aperture: SLINK4B
#define NV_DEVID_SLINK4B                         63

// Device Aperture: SPI
#define NV_DEVID_SPI                             64

// Device Aperture: DVC
#define NV_DEVID_DVC                             65

// Device Aperture: RTC
#define NV_DEVID_RTC                             66

// Device Aperture: KeyBoard Controller
#define NV_DEVID_KBC                             67

// Device Aperture: PMIF
#define NV_DEVID_PMIF                            68

// Device Aperture: FUSE
#define NV_DEVID_FUSE                            69

// Device Aperture: L2 Cache Controller
#define NV_DEVID_CMC                             70

// Device Apertuer: NOR FLASH Controller
#define NV_DEVID_NOR_REG                         71

// Device Aperture: EIDE
#define NV_DEVID_EIDE                            72

// Device Aperture: USB
#define NV_DEVID_USB                             73

// Device Aperture: SDIO
#define NV_DEVID_SDIO                            74

// Device Aperture: TVO
#define NV_DEVID_TVO                             75

// Device Aperture: DSI
#define NV_DEVID_DSI                             76

// Device Aperture: HDMI
#define NV_DEVID_HDMI                            77

// Device Aperture: Third Interrupt Controller extra registers
#define NV_DEVID_TRI_INTR                        78

// Device Aperture: Common Interrupt Controller
#define NV_DEVID_ICTLR                           79

// Non-Aperture Interrupt: DMA TX interrupts
#define NV_DEVID_DMA_TX_INTR                     80

// Non-Aperture Interrupt: DMA RX interrupts
#define NV_DEVID_DMA_RX_INTR                     81

// Non-Aperture Interrupt: SW reserved interrupt
#define NV_DEVID_SW_INTR                         82

// Non-Aperture Interrupt: CPU PMU Interrupt
#define NV_DEVID_CPU_INTR                        83

// Device Aperture: Timer Free Running MicroSecond
#define NV_DEVID_TMRUS                           84

// Device Aperture: Interrupt Controller ARB_GNT Registers
#define NV_DEVID_ICTLR_ARBGNT                    85

// Device Aperture: Interrupt Controller DMA Registers
#define NV_DEVID_ICTLR_DRQ                       86

// Device Aperture: AHB DMA Channel
#define NV_DEVID_AHB_DMA_CH                      87

// Device Aperture: APB DMA Channel
#define NV_DEVID_APB_DMA_CH                      88

// Device Aperture: AHB Arbitration Controller
#define NV_DEVID_AHB_ARBC                        89

// Obsolete - use AHB_ARBC
#define NV_DEVID_AHB_ARB_CTRL                    89

// Device Aperture: AHB/APB Debug Bus Registers
#define NV_DEVID_AHPBDEBUG                       91

// Device Aperture: Secure Boot Register
#define NV_DEVID_SECURE_BOOT                     92

// Device Aperture: SPROM
#define NV_DEVID_SPROM                           93

// Memory Aperture: AHB external memory remapping
#define NV_DEVID_AHB_EMEM                        94

// Non-Aperture Interrupt: External PMU interrupt
#define NV_DEVID_PMU_EXT                         95

// Device Aperture: AHB EMEM to MC Flush Register
#define NV_DEVID_PPCS                            96

// Device Aperture: MMU TLB registers for COP/AVP
#define NV_DEVID_MMU_TLB                         97

// Device Aperture: OVG engine
#define NV_DEVID_VG                              98

// Device Aperture: CSI
#define NV_DEVID_CSI                             99

// Device ID for COP
#define NV_DEVID_AVP                             100

// Device ID for MPCORE
#define NV_DEVID_CPU                             101

// Device Aperture: ULPI controller
#define NV_DEVID_ULPI                            102

// Device Aperture: ARM CONFIG registers
#define NV_DEVID_ARM_CONFIG                      103

// Device Aperture: ARM PL310 (L2 controller)
#define NV_DEVID_ARM_PL310                       104

// Device Aperture: PCIe
#define NV_DEVID_PCIE                            105

// Device Aperture: OWR (one wire)
#define NV_DEVID_OWR                             106

// Device Aperture: AVPUCQ
#define NV_DEVID_AVPUCQ                          107

// Device Aperture: AVPBSEA (obsolete)
#define NV_DEVID_AVPBSEA                         108

// Device Aperture: Sync NOR
#define NV_DEVID_SNOR                            109

// Device Aperture: SDMMC
#define NV_DEVID_SDMMC                           110

// Device Aperture: KFUSE
#define NV_DEVID_KFUSE                           111

// Device Aperture: CSITE
#define NV_DEVID_CSITE                           112

// Non-Aperture Interrupt: ARM Interprocessor Interrupt
#define NV_DEVID_ARM_IPI                         113

// Device Aperture: ARM Interrupts 0-31
#define NV_DEVID_ARM_ICTLR                       114

// Device Aperture: IOBIST
#define NV_DEVID_IOBIST                          115

// Device Aperture: SPEEDO
#define NV_DEVID_SPEEDO                          116

// Device Aperture: LA
#define NV_DEVID_LA                              117

// Device Aperture: VS
#define NV_DEVID_VS                              118

// Device Aperture: VCI
#define NV_DEVID_VCI                             119

// Device Aperture: APBIF
#define NV_DEVID_APBIF                           120

// Device Aperture: AHUB
#define NV_DEVID_AHUB                            121

// Device Aperture: DAM
#define NV_DEVID_DAM                             122

// ------------------------------------------------------------
//    hw powergroups
// ------------------------------------------------------------
// Always On
#define NV_POWERGROUP_AO                         0

// Main
#define NV_POWERGROUP_NPG                        1

// CPU related blocks
#define NV_POWERGROUP_CPU                        2

// 3D graphics
#define NV_POWERGROUP_TD                         3

// Video encode engine blocks
#define NV_POWERGROUP_VE                         4

// PCIe
#define NV_POWERGROUP_PCIE                       5

// Video decoder
#define NV_POWERGROUP_VDE                        6

// MPEG encoder
#define NV_POWERGROUP_MPE                        7

// SW define for Power Group maximum
#define NV_POWERGROUP_MAX                        8

// non-mapped power group
#define NV_POWERGROUP_INVALID                    0xffff

// SW table for mapping power group define to register enums (NV_POWERGROUP_INVALID = no mapping)
//  use as 'int table[NV_POWERGROUP_MAX] = { NV_POWERGROUP_ENUM_TABLE }'
#define NV_POWERGROUP_ENUM_TABLE                 NV_POWERGROUP_INVALID, NV_POWERGROUP_INVALID, 0, 1, 2, NV_POWERGROUP_INVALID, NV_POWERGROUP_INVALID, NV_POWERGROUP_INVALID

// ------------------------------------------------------------
//    relocation table data (stored in boot rom)
// ------------------------------------------------------------
// relocation table pointer stored at NV_RELOCATION_TABLE_OFFSET
#define NV_RELOCATION_TABLE_PTR_OFFSET 64
#define NV_RELOCATION_TABLE_SIZE  472
#define NV_RELOCATION_TABLE_INIT \
          0x00000001, 0x00020010, 0x00000000, 0x40000000, 0x005f1010, \
          0x00000000, 0x00000000, 0x00531010, 0x00000000, 0x00000000, \
          0x00521010, 0x00000000, 0x00000000, 0x00040010, 0x40000000, \
          0x00008000, 0x00040010, 0x40008000, 0x00008000, 0x00040010, \
          0x40010000, 0x00008000, 0x00040010, 0x40018000, 0x00008000, \
          0x00081010, 0x50000000, 0x00024000, 0x00091020, 0x50040000, \
          0x00002000, 0x000a1020, 0x50042000, 0x00001000, 0x000f1140, \
          0x54040000, 0x00040000, 0x00631040, 0x54080000, 0x00040000, \
          0x000d1140, 0x54080000, 0x00040000, 0x000e1040, 0x540c0000, \
          0x00040000, 0x00121040, 0x54100000, 0x00040000, 0x00111010, \
          0x54140000, 0x00040000, 0x00101230, 0x54180000, 0x00040000, \
          0x00131210, 0x54200000, 0x00040000, 0x00131210, 0x54240000, \
          0x00040000, 0x004d1110, 0x54280000, 0x00040000, 0x004b1010, \
          0x542c0000, 0x00040000, 0x004c1010, 0x54300000, 0x00040000, \
          0x00070010, 0x58000000, 0x01000000, 0x00141010, 0x60000000, \
          0x00001000, 0x00151010, 0x60001000, 0x00001000, 0x00161010, \
          0x60002000, 0x00001000, 0x00171010, 0x60003000, 0x00001000, \
          0x004f1010, 0x60004000, 0x00000040, 0x00551010, 0x60004040, \
          0x000000c0, 0x004f1110, 0x60004100, 0x00000040, 0x00561010, \
          0x60004140, 0x00000008, 0x00561110, 0x60004148, 0x00000008, \
          0x004f1210, 0x60004200, 0x00000040, 0x001a1010, 0x60005000, \
          0x00000008, 0x001a1010, 0x60005008, 0x00000008, 0x00541010, \
          0x60005010, 0x00000040, 0x001a1010, 0x60005050, 0x00000008, \
          0x001a1010, 0x60005058, 0x00000008, 0x001b1110, 0x60006000, \
          0x00001000, 0x001c1010, 0x60007000, 0x00000014, 0x001e1110, \
          0x60008000, 0x00001000, 0x00571010, 0x60009000, 0x00000020, \
          0x00571010, 0x60009020, 0x00000020, 0x00571010, 0x60009040, \
          0x00000020, 0x00571010, 0x60009060, 0x00000020, 0x001f1010, \
          0x6000a000, 0x00001000, 0x00581010, 0x6000b000, 0x00000020, \
          0x00581010, 0x6000b020, 0x00000020, 0x00581010, 0x6000b040, \
          0x00000020, 0x00581010, 0x6000b060, 0x00000020, 0x00581010, \
          0x6000b080, 0x00000020, 0x00581010, 0x6000b0a0, 0x00000020, \
          0x00581010, 0x6000b0c0, 0x00000020, 0x00581010, 0x6000b0e0, \
          0x00000020, 0x00581010, 0x6000b100, 0x00000020, 0x00581010, \
          0x6000b120, 0x00000020, 0x00581010, 0x6000b140, 0x00000020, \
          0x00581010, 0x6000b160, 0x00000020, 0x00581010, 0x6000b180, \
          0x00000020, 0x00581010, 0x6000b1a0, 0x00000020, 0x00581010, \
          0x6000b1c0, 0x00000020, 0x00581010, 0x6000b1e0, 0x00000020, \
          0x00201010, 0x6000c000, 0x00000400, 0x00591010, 0x6000c004, \
          0x0000010c, 0x005b1010, 0x6000c150, 0x000000a6, 0x005c1010, \
          0x6000c200, 0x00000004, 0x00211010, 0x6000c400, 0x00000400, \
          0x00222010, 0x6000d000, 0x00000880, 0x00222010, 0x6000d080, \
          0x00000880, 0x00222010, 0x6000d100, 0x00000880, 0x00222010, \
          0x6000d180, 0x00000880, 0x00222010, 0x6000d200, 0x00000880, \
          0x00222010, 0x6000d280, 0x00000880, 0x00231010, 0x6000e000, \
          0x00001000, 0x00241010, 0x6000f000, 0x00001000, 0x002f1010, \
          0x6001a000, 0x00003b00, 0x00261110, 0x6001a000, 0x00003b00, \
          0x00311110, 0x70000000, 0x00001000, 0x00321010, 0x70002000, \
          0x00000200, 0x00331010, 0x70002400, 0x00000200, 0x00341010, \
          0x70002800, 0x00000100, 0x00341010, 0x70002a00, 0x00000100, \
          0x00351110, 0x70006000, 0x00000040, 0x00351110, 0x70006040, \
          0x00000040, 0x00361010, 0x70006100, 0x00000100, 0x00351110, \
          0x70006200, 0x00000100, 0x00371110, 0x70008000, 0x00000100, \
          0x00381010, 0x70008500, 0x00000100, 0x00391010, 0x70008a00, \
          0x00000200, 0x003a1010, 0x7000a000, 0x00000100, 0x003b1010, \
          0x7000b000, 0x00000100, 0x003c1110, 0x7000c000, 0x00000100, \
          0x003d1010, 0x7000c100, 0x00000100, 0x00401010, 0x7000c380, \
          0x00000030, 0x003c1110, 0x7000c400, 0x00000100, 0x00411010, \
          0x7000d000, 0x00000200, 0x003f1010, 0x7000d300, 0x00000100, \
          0x003e1010, 0x7000d400, 0x00000200, 0x003e1010, 0x7000d600, \
          0x00000200, 0x003e1010, 0x7000d800, 0x00000200, 0x00421000, \
          0x7000e000, 0x00000100, 0x00431000, 0x7000e200, 0x00000100, \
          0x00441000, 0x7000e400, 0x00000100, 0x00461010, 0x7000e800, \
          0x00000200, 0x005d1010, 0x7000ec00, 0x00000100, 0x000b1010, \
          0x7000f000, 0x00000400, 0x000c1110, 0x7000f400, 0x00000400, \
          0x00451110, 0x7000f800, 0x00000400, 0x00050010, 0x80000000, \
          0x10000000, 0x005e0010, 0x90000000, 0x20000000, 0x00060010, \
          0xb0000000, 0x08000000, 0x00060010, 0xb8000000, 0x08000000, \
          0x00481010, 0xc3000000, 0x01000000, 0x00601010, 0xc4000000, \
          0x00010000, 0x00491110, 0xc5000000, 0x00002000, 0x004a1010, \
          0xc8000000, 0x00000100, 0x004a1010, 0xc8000100, 0x00000100, \
          0x00611010, 0xf000f000, 0x00001000, 0x00000000, 0x82100116, \
          0x81e00218, 0x8210031f, 0xc2100800, 0xa2100801, 0xc2100802, \
          0xa2100803, 0x82100b04, 0x82100d05, 0x82100e06, 0x82100f07, \
          0x82101008, 0x82101209, 0x8210130a, 0x8210140b, 0x8210150c, \
          0xa1c01904, 0xc1c01905, 0xc1c01906, 0xa1c01907, 0xa1c01a1d, \
          0xc1c01a1c, 0x81e01f1e, 0x81e0201f, 0x81c02200, 0x81c02301, \
          0x81e02509, 0x81e0260a, 0xa1e0280b, 0xc1e0280c, 0xa1c0291b, \
          0xc1e0291d, 0xa1c02e1a, 0xc1e02e1c, 0x81e04316, 0x81e04400, \
          0x81e04501, 0x81e04602, 0x81e04703, 0x81e04817, 0x82104917, \
          0x81c04a19, 0x81c04d09, 0x81c04d0a, 0x81c04d0b, 0x81c04d0c, \
          0x81c04d08, 0x81c04d11, 0x82104e10, 0x82104e18, 0x82104f11, \
          0x81f04f00, 0x81f04f12, 0x82004f00, 0x82004f12, 0x81e0500d, \
          0x81f05003, 0x81f05004, 0x82005003, 0x82005004, 0x81c0510d, \
          0x81f05102, 0x81f05101, 0x82005101, 0x82005102, 0x81c05203, \
          0x81f05206, 0x81f05205, 0x82005205, 0x82005206, 0x81e05304, \
          0x81f05308, 0x82005308, 0x81e05405, 0x81f05409, 0x82005409, \
          0x81e05514, 0x81f05511, 0x82005511, 0x81e0560e, 0x81f0560a, \
          0x8200560a, 0x81c05718, 0x81c05816, 0x81c05910, 0x81e05b0f, \
          0x81e05c06, 0x81f05c0c, 0x82005c0c, 0x81e05d08, 0x81f05d0b, \
          0x82005d0b, 0x81e05e07, 0x81f05e07, 0x82005e07, 0x82105f14, \
          0x81f05f0d, 0x82005f0d, 0x81e06015, 0x81e0611a, 0x81e0621b, \
          0x82106312, 0x82106413, 0x81c06502, 0x82106615, 0x8210680f, \
          0x82106a0d, 0x82106b0e, 0x81c07117, 0x81c07314, 0x81c0740e, \
          0x81c0750f, 0x00000000
#endif
