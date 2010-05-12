/*
 * Copyright (c) 2007-2009 NVIDIA Corporation.
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

#ifndef INCLUDED_NVRM_HW_DEVIDS_H
#define INCLUDED_NVRM_HW_DEVIDS_H

// Memory Aperture: Internal Memory
#define NVRM_DEVID_IMEM                            1

// Memory Aperture: External Memory
#define NVRM_DEVID_EMEM                            2

// Memory Aperture: TCRAM
#define NVRM_DEVID_TCRAM                           3

// Memory Aperture: IRAM
#define NVRM_DEVID_IRAM                            4

// Memory Aperture: NOR FLASH
#define NVRM_DEVID_NOR                             5

// Memory Aperture: EXIO
#define NVRM_DEVID_EXIO                            6

// Memory Aperture: GART
#define NVRM_DEVID_GART                            7

// Device Aperture: Graphics Host (HOST1X)
#define NVRM_DEVID_HOST1X                          8

// Device Aperture: ARM PERIPH registers
#define NVRM_DEVID_ARM_PERIPH                      9

// Device Aperture: MSELECT
#define NVRM_DEVID_MSELECT                         10

// Device Aperture: memory controller
#define NVRM_DEVID_MC                              11

// Device Aperture: external memory controller
#define NVRM_DEVID_EMC                             12

// Device Aperture: video input
#define NVRM_DEVID_VI                              13

// Device Aperture: encoder pre-processor
#define NVRM_DEVID_EPP                             14

// Device Aperture: video encoder
#define NVRM_DEVID_MPE                             15

// Device Aperture: 3D engine
#define NVRM_DEVID_GR3D                            16

// Device Aperture: 2D + SBLT engine
#define NVRM_DEVID_GR2D                            17

// Device Aperture: Image Signal Processor
#define NVRM_DEVID_ISP                             18

// Device Aperture: DISPLAY
#define NVRM_DEVID_DISPLAY                         19

// Device Aperture: UPTAG
#define NVRM_DEVID_UPTAG                           20

// Device Aperture - SHR_SEM
#define NVRM_DEVID_SHR_SEM                         21

// Device Aperture - ARB_SEM
#define NVRM_DEVID_ARB_SEM                         22

// Device Aperture - ARB_PRI
#define NVRM_DEVID_ARB_PRI                         23

// Obsoleted for AP15
#define NVRM_DEVID_PRI_INTR                        24

// Obsoleted for AP15
#define NVRM_DEVID_SEC_INTR                        25

// Device Aperture: Timer Programmable
#define NVRM_DEVID_TMR                             26

// Device Aperture: Clock and Reset
#define NVRM_DEVID_CAR                             27

// Device Aperture: Flow control
#define NVRM_DEVID_FLOW                            28

// Device Aperture: Event
#define NVRM_DEVID_EVENT                           29

// Device Aperture: AHB DMA
#define NVRM_DEVID_AHB_DMA                         30

// Device Aperture: APB DMA
#define NVRM_DEVID_APB_DMA                         31

// Obsolete - use AVP_CACHE
#define NVRM_DEVID_CC                              32

// Device Aperture: COP Cache Controller
#define NVRM_DEVID_AVP_CACHE                       32

// Device Aperture: SYS_REG
#define NVRM_DEVID_SYS_REG                         32

// Device Aperture: System Statistic monitor
#define NVRM_DEVID_STAT                            33

// Device Aperture: GPIO
#define NVRM_DEVID_GPIO                            34

// Device Aperture: Vector Co-Processor 2
#define NVRM_DEVID_VCP                             35

// Device Aperture: Arm Vectors
#define NVRM_DEVID_VECTOR                          36

// Device: MEM
#define NVRM_DEVID_MEM                             37

// Obsolete - use VDE
#define NVRM_DEVID_SXE                             38

// Device Aperture: video decoder
#define NVRM_DEVID_VDE                             38

// Obsolete - use VDE
#define NVRM_DEVID_BSEV                            39

// Obsolete - use VDE
#define NVRM_DEVID_MBE                             40

// Obsolete - use VDE
#define NVRM_DEVID_PPE                             41

// Obsolete - use VDE
#define NVRM_DEVID_MCE                             42

// Obsolete - use VDE
#define NVRM_DEVID_TFE                             43

// Obsolete - use VDE
#define NVRM_DEVID_PPB                             44

// Obsolete - use VDE
#define NVRM_DEVID_VDMA                            45

// Obsolete - use VDE
#define NVRM_DEVID_UCQ                             46

// Device Aperture: BSEA (now in AVP cluster)
#define NVRM_DEVID_BSEA                            47

// Obsolete - use VDE
#define NVRM_DEVID_FRAMEID                         48

// Device Aperture: Misc regs
#define NVRM_DEVID_MISC                            49

// Obsolete
#define NVRM_DEVID_AC97                            50

// Device Aperture: S/P-DIF
#define NVRM_DEVID_SPDIF                           51

// Device Aperture: I2S
#define NVRM_DEVID_I2S                             52

// Device Aperture: UART
#define NVRM_DEVID_UART                            53

// Device Aperture: VFIR
#define NVRM_DEVID_VFIR                            54

// Device Aperture: NAND Flash Controller
#define NVRM_DEVID_NANDCTRL                        55

// Obsolete - use NANDCTRL
#define NVRM_DEVID_NANDFLASH                       55

// Device Aperture: HSMMC
#define NVRM_DEVID_HSMMC                           56

// Device Aperture: XIO
#define NVRM_DEVID_XIO                             57

// Device Aperture: PWFM
#define NVRM_DEVID_PWFM                            58

// Device Aperture: MIPI
#define NVRM_DEVID_MIPI_HS                         59

// Device Aperture: I2C
#define NVRM_DEVID_I2C                             60

// Device Aperture: TWC
#define NVRM_DEVID_TWC                             61

// Device Aperture: SLINK
#define NVRM_DEVID_SLINK                           62

// Device Aperture: SLINK4B
#define NVRM_DEVID_SLINK4B                         63

// Device Aperture: SPI
#define NVRM_DEVID_SPI                             64

// Device Aperture: DTV
#define NVRM_DEVID_DTV                             64

// Device Aperture: DVC
#define NVRM_DEVID_DVC                             65

// Device Aperture: RTC
#define NVRM_DEVID_RTC                             66

// Device Aperture: KeyBoard Controller
#define NVRM_DEVID_KBC                             67

// Device Aperture: PMIF
#define NVRM_DEVID_PMIF                            68

// Device Aperture: FUSE
#define NVRM_DEVID_FUSE                            69

// Device Aperture: L2 Cache Controller
#define NVRM_DEVID_CMC                             70

// Device Apertuer: NOR FLASH Controller
#define NVRM_DEVID_NOR_REG                         71

// Device Aperture: EIDE
#define NVRM_DEVID_EIDE                            72

// Device Aperture: USB
#define NVRM_DEVID_USB                             73

// Device Aperture: SDIO
#define NVRM_DEVID_SDIO                            74

// Device Aperture: TVO
#define NVRM_DEVID_TVO                             75

// Device Aperture: DSI
#define NVRM_DEVID_DSI                             76

// Device Aperture: HDMI
#define NVRM_DEVID_HDMI                            77

// Device Aperture: Third Interrupt Controller extra registers
#define NVRM_DEVID_TRI_INTR                        78

// Device Aperture: Common Interrupt Controller
#define NVRM_DEVID_ICTLR                           79

// Non-Aperture Interrupt: DMA TX interrupts
#define NVRM_DEVID_DMA_TX_INTR                     80

// Non-Aperture Interrupt: DMA RX interrupts
#define NVRM_DEVID_DMA_RX_INTR                     81

// Non-Aperture Interrupt: SW reserved interrupt
#define NVRM_DEVID_SW_INTR                         82

// Non-Aperture Interrupt: CPU PMU Interrupt
#define NVRM_DEVID_CPU_INTR                        83

// Device Aperture: Timer Free Running MicroSecond
#define NVRM_DEVID_TMRUS                           84

// Device Aperture: Interrupt Controller ARB_GNT Registers
#define NVRM_DEVID_ICTLR_ARBGNT                    85

// Device Aperture: Interrupt Controller DMA Registers
#define NVRM_DEVID_ICTLR_DRQ                       86

// Device Aperture: AHB DMA Channel
#define NVRM_DEVID_AHB_DMA_CH                      87

// Device Aperture: APB DMA Channel
#define NVRM_DEVID_APB_DMA_CH                      88

// Device Aperture: AHB Arbitration Controller
#define NVRM_DEVID_AHB_ARBC                        89

// Obsolete - use AHB_ARBC
#define NVRM_DEVID_AHB_ARB_CTRL                    89

// Device Aperture: AHB/APB Debug Bus Registers
#define NVRM_DEVID_AHPBDEBUG                       91

// Device Aperture: Secure Boot Register
#define NVRM_DEVID_SECURE_BOOT                     92

// Device Aperture: SPROM
#define NVRM_DEVID_SPROM                           93

// Memory Aperture: AHB external memory remapping
#define NVRM_DEVID_AHB_EMEM                        94

// Non-Aperture Interrupt: External PMU interrupt
#define NVRM_DEVID_PMU_EXT                         95

// Device Aperture: AHB EMEM to MC Flush Register
#define NVRM_DEVID_PPCS                            96

// Device Aperture: MMU TLB registers for COP/AVP
#define NVRM_DEVID_MMU_TLB                         97

// Device Aperture: OVG engine
#define NVRM_DEVID_VG                              98

// Device Aperture: CSI
#define NVRM_DEVID_CSI                             99

// Device ID for COP
#define NVRM_DEVID_AVP                             100

// Device ID for MPCORE
#define NVRM_DEVID_CPU                             101

// Device Aperture: ULPI controller
#define NVRM_DEVID_ULPI                            102

// Device Aperture: ARM CONFIG registers
#define NVRM_DEVID_ARM_CONFIG                      103

// Device Aperture: ARM PL310 (L2 controller)
#define NVRM_DEVID_ARM_PL310                       104

// Device Aperture: PCIe
#define NVRM_DEVID_PCIE                            105

// Device Aperture: OWR (one wire)
#define NVRM_DEVID_OWR                             106

// Device Aperture: AVPUCQ
#define NVRM_DEVID_AVPUCQ                          107

// Device Aperture: AVPBSEA (obsolete)
#define NVRM_DEVID_AVPBSEA                         108

// Device Aperture: Sync NOR
#define NVRM_DEVID_SNOR                            109

// Device Aperture: SDMMC
#define NVRM_DEVID_SDMMC                           110

// Device Aperture: KFUSE
#define NVRM_DEVID_KFUSE                           111

// Device Aperture: CSITE
#define NVRM_DEVID_CSITE                           112

// Non-Aperture Interrupt: ARM Interprocessor Interrupt
#define NVRM_DEVID_ARM_IPI                         113

// Device Aperture: ARM Interrupts 0-31
#define NVRM_DEVID_ARM_ICTLR                       114

// Device Aperture: IOBIST
#define NVRM_DEVID_IOBIST                          115

// Device Aperture: SPEEDO
#define NVRM_DEVID_SPEEDO                          116

// Device Aperture: LA
#define NVRM_DEVID_LA                              117

// Device Aperture: VS
#define NVRM_DEVID_VS                              118

// Device Aperture: VCI
#define NVRM_DEVID_VCI                             119

// Device Aperture: APBIF
#define NVRM_DEVID_APBIF                           120

// Device Aperture: AUDIO
#define NVRM_DEVID_AUDIO                           121

// Device Aperture: DAM
#define NVRM_DEVID_DAM                             122

// Device Aperture: TSENSOR
#define NVRM_DEVID_TSENSOR                         123

// Device Aperture: SE
#define NVRM_DEVID_SE                              124

// Device Aperture: TZRAM
#define NVRM_DEVID_TZRAM                           125

// Device Aperture: AUDIO_CLUSTER
#define NVRM_DEVID_AUDIO_CLUSTER                   126

// Device Aperture: HDA
#define NVRM_DEVID_HDA                             127

// Device Aperture: SATA
#define NVRM_DEVID_SATA                            128

// Device Aperture: ATOMICS
#define NVRM_DEVID_ATOMICS                         129

// Device Aperture: IPATCH
#define NVRM_DEVID_IPATCH                          130

// Device Aperture: Activity Monitor
#define NVRM_DEVID_ACTMON                          131

// Device Aperture: Watch Dog Timer
#define NVRM_DEVID_WDT                             132

#endif // INCLUDED_NVRM_HW_DEVIDS_H
