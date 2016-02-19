#ifndef DDK750_REG_H__
#define DDK750_REG_H__

/* New register for SM750LE */
#define DE_STATE1                                        0x100054
#define DE_STATE1_DE_ABORT                               BIT(0)

#define DE_STATE2                                        0x100058
#define DE_STATE2_DE_FIFO_EMPTY                          BIT(3)
#define DE_STATE2_DE_STATUS_BUSY                         BIT(2)
#define DE_STATE2_DE_MEM_FIFO_EMPTY                      BIT(1)

#define SYSTEM_CTRL                                   0x000000
#define SYSTEM_CTRL_DPMS_MASK                         (0x3 << 30)
#define SYSTEM_CTRL_DPMS_VPHP                         (0x0 << 30)
#define SYSTEM_CTRL_DPMS_VPHN                         (0x1 << 30)
#define SYSTEM_CTRL_DPMS_VNHP                         (0x2 << 30)
#define SYSTEM_CTRL_DPMS_VNHN                         (0x3 << 30)
#define SYSTEM_CTRL_PCI_BURST                         BIT(29)
#define SYSTEM_CTRL_PCI_MASTER                        BIT(25)
#define SYSTEM_CTRL_LATENCY_TIMER_OFF                 BIT(24)
#define SYSTEM_CTRL_DE_FIFO_EMPTY                     BIT(23)
#define SYSTEM_CTRL_DE_STATUS_BUSY                    BIT(22)
#define SYSTEM_CTRL_DE_MEM_FIFO_EMPTY                 BIT(21)
#define SYSTEM_CTRL_CSC_STATUS_BUSY                   BIT(20)
#define SYSTEM_CTRL_CRT_VSYNC_ACTIVE                  BIT(19)
#define SYSTEM_CTRL_PANEL_VSYNC_ACTIVE                BIT(18)
#define SYSTEM_CTRL_CURRENT_BUFFER_FLIP_PENDING       BIT(17)
#define SYSTEM_CTRL_DMA_STATUS_BUSY                   BIT(16)
#define SYSTEM_CTRL_PCI_BURST_READ                    BIT(15)
#define SYSTEM_CTRL_DE_ABORT                          BIT(13)
#define SYSTEM_CTRL_PCI_SUBSYS_ID_LOCK                BIT(11)
#define SYSTEM_CTRL_PCI_RETRY_OFF                     BIT(7)
#define SYSTEM_CTRL_PCI_SLAVE_BURST_READ_SIZE_MASK    (0x3 << 4)
#define SYSTEM_CTRL_PCI_SLAVE_BURST_READ_SIZE_1       (0x0 << 4)
#define SYSTEM_CTRL_PCI_SLAVE_BURST_READ_SIZE_2       (0x1 << 4)
#define SYSTEM_CTRL_PCI_SLAVE_BURST_READ_SIZE_4       (0x2 << 4)
#define SYSTEM_CTRL_PCI_SLAVE_BURST_READ_SIZE_8       (0x3 << 4)
#define SYSTEM_CTRL_CRT_TRISTATE                      BIT(3)
#define SYSTEM_CTRL_PCIMEM_TRISTATE                   BIT(2)
#define SYSTEM_CTRL_LOCALMEM_TRISTATE                 BIT(1)
#define SYSTEM_CTRL_PANEL_TRISTATE                    BIT(0)

#define MISC_CTRL                                     0x000004
#define MISC_CTRL_DRAM_RERESH_COUNT                   BIT(27)
#define MISC_CTRL_DRAM_REFRESH_TIME_MASK              (0x3 << 25)
#define MISC_CTRL_DRAM_REFRESH_TIME_8                 (0x0 << 25)
#define MISC_CTRL_DRAM_REFRESH_TIME_16                (0x1 << 25)
#define MISC_CTRL_DRAM_REFRESH_TIME_32                (0x2 << 25)
#define MISC_CTRL_DRAM_REFRESH_TIME_64                (0x3 << 25)
#define MISC_CTRL_INT_OUTPUT_INVERT                   BIT(24)
#define MISC_CTRL_PLL_CLK_COUNT                       BIT(23)
#define MISC_CTRL_DAC_POWER_OFF                       BIT(20)
#define MISC_CTRL_CLK_SELECT_TESTCLK                  BIT(16)
#define MISC_CTRL_DRAM_COLUMN_SIZE_MASK               (0x3 << 14)
#define MISC_CTRL_DRAM_COLUMN_SIZE_256                (0x0 << 14)
#define MISC_CTRL_DRAM_COLUMN_SIZE_512                (0x1 << 14)
#define MISC_CTRL_DRAM_COLUMN_SIZE_1024               (0x2 << 14)
#define MISC_CTRL_LOCALMEM_SIZE_MASK                  (0x3 << 12)
#define MISC_CTRL_LOCALMEM_SIZE_8M                    (0x3 << 12)
#define MISC_CTRL_LOCALMEM_SIZE_16M                   (0x0 << 12)
#define MISC_CTRL_LOCALMEM_SIZE_32M                   (0x1 << 12)
#define MISC_CTRL_LOCALMEM_SIZE_64M                   (0x2 << 12)
#define MISC_CTRL_DRAM_TWTR                           BIT(11)
#define MISC_CTRL_DRAM_TWR                            BIT(10)
#define MISC_CTRL_DRAM_TRP                            BIT(9)
#define MISC_CTRL_DRAM_TRFC                           BIT(8)
#define MISC_CTRL_DRAM_TRAS                           BIT(7)
#define MISC_CTRL_LOCALMEM_RESET                      BIT(6)
#define MISC_CTRL_LOCALMEM_STATE_INACTIVE             BIT(5)
#define MISC_CTRL_CPU_CAS_LATENCY                     BIT(4)
#define MISC_CTRL_DLL_OFF                             BIT(3)
#define MISC_CTRL_DRAM_OUTPUT_HIGH                    BIT(2)
#define MISC_CTRL_LOCALMEM_BUS_SIZE                   BIT(1)
#define MISC_CTRL_EMBEDDED_LOCALMEM_OFF               BIT(0)

#define GPIO_MUX                                      0x000008
#define GPIO_MUX_31                                   BIT(31)
#define GPIO_MUX_30                                   BIT(30)
#define GPIO_MUX_29                                   BIT(29)
#define GPIO_MUX_28                                   BIT(28)
#define GPIO_MUX_27                                   BIT(27)
#define GPIO_MUX_26                                   BIT(26)
#define GPIO_MUX_25                                   BIT(25)
#define GPIO_MUX_24                                   BIT(24)
#define GPIO_MUX_23                                   BIT(23)
#define GPIO_MUX_22                                   BIT(22)
#define GPIO_MUX_21                                   BIT(21)
#define GPIO_MUX_20                                   BIT(20)
#define GPIO_MUX_19                                   BIT(19)
#define GPIO_MUX_18                                   BIT(18)
#define GPIO_MUX_17                                   BIT(17)
#define GPIO_MUX_16                                   BIT(16)
#define GPIO_MUX_15                                   BIT(15)
#define GPIO_MUX_14                                   BIT(14)
#define GPIO_MUX_13                                   BIT(13)
#define GPIO_MUX_12                                   BIT(12)
#define GPIO_MUX_11                                   BIT(11)
#define GPIO_MUX_10                                   BIT(10)
#define GPIO_MUX_9                                    BIT(9)
#define GPIO_MUX_8                                    BIT(8)
#define GPIO_MUX_7                                    BIT(7)
#define GPIO_MUX_6                                    BIT(6)
#define GPIO_MUX_5                                    BIT(5)
#define GPIO_MUX_4                                    BIT(4)
#define GPIO_MUX_3                                    BIT(3)
#define GPIO_MUX_2                                    BIT(2)
#define GPIO_MUX_1                                    BIT(1)
#define GPIO_MUX_0                                    BIT(0)

#define LOCALMEM_ARBITRATION                          0x00000C
#define LOCALMEM_ARBITRATION_ROTATE                   BIT(28)
#define LOCALMEM_ARBITRATION_VGA_MASK                 (0x7 << 24)
#define LOCALMEM_ARBITRATION_VGA_OFF                  (0x0 << 24)
#define LOCALMEM_ARBITRATION_VGA_PRIORITY_1           (0x1 << 24)
#define LOCALMEM_ARBITRATION_VGA_PRIORITY_2           (0x2 << 24)
#define LOCALMEM_ARBITRATION_VGA_PRIORITY_3           (0x3 << 24)
#define LOCALMEM_ARBITRATION_VGA_PRIORITY_4           (0x4 << 24)
#define LOCALMEM_ARBITRATION_VGA_PRIORITY_5           (0x5 << 24)
#define LOCALMEM_ARBITRATION_VGA_PRIORITY_6           (0x6 << 24)
#define LOCALMEM_ARBITRATION_VGA_PRIORITY_7           (0x7 << 24)
#define LOCALMEM_ARBITRATION_DMA_MASK                 (0x7 << 20)
#define LOCALMEM_ARBITRATION_DMA_OFF                  (0x0 << 20)
#define LOCALMEM_ARBITRATION_DMA_PRIORITY_1           (0x1 << 20)
#define LOCALMEM_ARBITRATION_DMA_PRIORITY_2           (0x2 << 20)
#define LOCALMEM_ARBITRATION_DMA_PRIORITY_3           (0x3 << 20)
#define LOCALMEM_ARBITRATION_DMA_PRIORITY_4           (0x4 << 20)
#define LOCALMEM_ARBITRATION_DMA_PRIORITY_5           (0x5 << 20)
#define LOCALMEM_ARBITRATION_DMA_PRIORITY_6           (0x6 << 20)
#define LOCALMEM_ARBITRATION_DMA_PRIORITY_7           (0x7 << 20)
#define LOCALMEM_ARBITRATION_ZVPORT1_MASK             (0x7 << 16)
#define LOCALMEM_ARBITRATION_ZVPORT1_OFF              (0x0 << 16)
#define LOCALMEM_ARBITRATION_ZVPORT1_PRIORITY_1       (0x1 << 16)
#define LOCALMEM_ARBITRATION_ZVPORT1_PRIORITY_2       (0x2 << 16)
#define LOCALMEM_ARBITRATION_ZVPORT1_PRIORITY_3       (0x3 << 16)
#define LOCALMEM_ARBITRATION_ZVPORT1_PRIORITY_4       (0x4 << 16)
#define LOCALMEM_ARBITRATION_ZVPORT1_PRIORITY_5       (0x5 << 16)
#define LOCALMEM_ARBITRATION_ZVPORT1_PRIORITY_6       (0x6 << 16)
#define LOCALMEM_ARBITRATION_ZVPORT1_PRIORITY_7       (0x7 << 16)
#define LOCALMEM_ARBITRATION_ZVPORT0_MASK             (0x7 << 12)
#define LOCALMEM_ARBITRATION_ZVPORT0_OFF              (0x0 << 12)
#define LOCALMEM_ARBITRATION_ZVPORT0_PRIORITY_1       (0x1 << 12)
#define LOCALMEM_ARBITRATION_ZVPORT0_PRIORITY_2       (0x2 << 12)
#define LOCALMEM_ARBITRATION_ZVPORT0_PRIORITY_3       (0x3 << 12)
#define LOCALMEM_ARBITRATION_ZVPORT0_PRIORITY_4       (0x4 << 12)
#define LOCALMEM_ARBITRATION_ZVPORT0_PRIORITY_5       (0x5 << 12)
#define LOCALMEM_ARBITRATION_ZVPORT0_PRIORITY_6       (0x6 << 12)
#define LOCALMEM_ARBITRATION_ZVPORT0_PRIORITY_7       (0x7 << 12)
#define LOCALMEM_ARBITRATION_VIDEO_MASK               (0x7 << 8)
#define LOCALMEM_ARBITRATION_VIDEO_OFF                (0x0 << 8)
#define LOCALMEM_ARBITRATION_VIDEO_PRIORITY_1         (0x1 << 8)
#define LOCALMEM_ARBITRATION_VIDEO_PRIORITY_2         (0x2 << 8)
#define LOCALMEM_ARBITRATION_VIDEO_PRIORITY_3         (0x3 << 8)
#define LOCALMEM_ARBITRATION_VIDEO_PRIORITY_4         (0x4 << 8)
#define LOCALMEM_ARBITRATION_VIDEO_PRIORITY_5         (0x5 << 8)
#define LOCALMEM_ARBITRATION_VIDEO_PRIORITY_6         (0x6 << 8)
#define LOCALMEM_ARBITRATION_VIDEO_PRIORITY_7         (0x7 << 8)
#define LOCALMEM_ARBITRATION_PANEL_MASK               (0x7 << 4)
#define LOCALMEM_ARBITRATION_PANEL_OFF                (0x0 << 4)
#define LOCALMEM_ARBITRATION_PANEL_PRIORITY_1         (0x1 << 4)
#define LOCALMEM_ARBITRATION_PANEL_PRIORITY_2         (0x2 << 4)
#define LOCALMEM_ARBITRATION_PANEL_PRIORITY_3         (0x3 << 4)
#define LOCALMEM_ARBITRATION_PANEL_PRIORITY_4         (0x4 << 4)
#define LOCALMEM_ARBITRATION_PANEL_PRIORITY_5         (0x5 << 4)
#define LOCALMEM_ARBITRATION_PANEL_PRIORITY_6         (0x6 << 4)
#define LOCALMEM_ARBITRATION_PANEL_PRIORITY_7         (0x7 << 4)
#define LOCALMEM_ARBITRATION_CRT_MASK                 0x7
#define LOCALMEM_ARBITRATION_CRT_OFF                  0x0
#define LOCALMEM_ARBITRATION_CRT_PRIORITY_1           0x1
#define LOCALMEM_ARBITRATION_CRT_PRIORITY_2           0x2
#define LOCALMEM_ARBITRATION_CRT_PRIORITY_3           0x3
#define LOCALMEM_ARBITRATION_CRT_PRIORITY_4           0x4
#define LOCALMEM_ARBITRATION_CRT_PRIORITY_5           0x5
#define LOCALMEM_ARBITRATION_CRT_PRIORITY_6           0x6
#define LOCALMEM_ARBITRATION_CRT_PRIORITY_7           0x7

#define PCIMEM_ARBITRATION                            0x000010
#define PCIMEM_ARBITRATION_ROTATE                     BIT(28)
#define PCIMEM_ARBITRATION_VGA_MASK                   (0x7 << 24)
#define PCIMEM_ARBITRATION_VGA_OFF                    (0x0 << 24)
#define PCIMEM_ARBITRATION_VGA_PRIORITY_1             (0x1 << 24)
#define PCIMEM_ARBITRATION_VGA_PRIORITY_2             (0x2 << 24)
#define PCIMEM_ARBITRATION_VGA_PRIORITY_3             (0x3 << 24)
#define PCIMEM_ARBITRATION_VGA_PRIORITY_4             (0x4 << 24)
#define PCIMEM_ARBITRATION_VGA_PRIORITY_5             (0x5 << 24)
#define PCIMEM_ARBITRATION_VGA_PRIORITY_6             (0x6 << 24)
#define PCIMEM_ARBITRATION_VGA_PRIORITY_7             (0x7 << 24)
#define PCIMEM_ARBITRATION_DMA_MASK                   (0x7 << 20)
#define PCIMEM_ARBITRATION_DMA_OFF                    (0x0 << 20)
#define PCIMEM_ARBITRATION_DMA_PRIORITY_1             (0x1 << 20)
#define PCIMEM_ARBITRATION_DMA_PRIORITY_2             (0x2 << 20)
#define PCIMEM_ARBITRATION_DMA_PRIORITY_3             (0x3 << 20)
#define PCIMEM_ARBITRATION_DMA_PRIORITY_4             (0x4 << 20)
#define PCIMEM_ARBITRATION_DMA_PRIORITY_5             (0x5 << 20)
#define PCIMEM_ARBITRATION_DMA_PRIORITY_6             (0x6 << 20)
#define PCIMEM_ARBITRATION_DMA_PRIORITY_7             (0x7 << 20)
#define PCIMEM_ARBITRATION_ZVPORT1_MASK               (0x7 << 16)
#define PCIMEM_ARBITRATION_ZVPORT1_OFF                (0x0 << 16)
#define PCIMEM_ARBITRATION_ZVPORT1_PRIORITY_1         (0x1 << 16)
#define PCIMEM_ARBITRATION_ZVPORT1_PRIORITY_2         (0x2 << 16)
#define PCIMEM_ARBITRATION_ZVPORT1_PRIORITY_3         (0x3 << 16)
#define PCIMEM_ARBITRATION_ZVPORT1_PRIORITY_4         (0x4 << 16)
#define PCIMEM_ARBITRATION_ZVPORT1_PRIORITY_5         (0x5 << 16)
#define PCIMEM_ARBITRATION_ZVPORT1_PRIORITY_6         (0x6 << 16)
#define PCIMEM_ARBITRATION_ZVPORT1_PRIORITY_7         (0x7 << 16)
#define PCIMEM_ARBITRATION_ZVPORT0_MASK               (0x7 << 12)
#define PCIMEM_ARBITRATION_ZVPORT0_OFF                (0x0 << 12)
#define PCIMEM_ARBITRATION_ZVPORT0_PRIORITY_1         (0x1 << 12)
#define PCIMEM_ARBITRATION_ZVPORT0_PRIORITY_2         (0x2 << 12)
#define PCIMEM_ARBITRATION_ZVPORT0_PRIORITY_3         (0x3 << 12)
#define PCIMEM_ARBITRATION_ZVPORT0_PRIORITY_4         (0x4 << 12)
#define PCIMEM_ARBITRATION_ZVPORT0_PRIORITY_5         (0x5 << 12)
#define PCIMEM_ARBITRATION_ZVPORT0_PRIORITY_6         (0x6 << 12)
#define PCIMEM_ARBITRATION_ZVPORT0_PRIORITY_7         (0x7 << 12)
#define PCIMEM_ARBITRATION_VIDEO_MASK                 (0x7 << 8)
#define PCIMEM_ARBITRATION_VIDEO_OFF                  (0x0 << 8)
#define PCIMEM_ARBITRATION_VIDEO_PRIORITY_1           (0x1 << 8)
#define PCIMEM_ARBITRATION_VIDEO_PRIORITY_2           (0x2 << 8)
#define PCIMEM_ARBITRATION_VIDEO_PRIORITY_3           (0x3 << 8)
#define PCIMEM_ARBITRATION_VIDEO_PRIORITY_4           (0x4 << 8)
#define PCIMEM_ARBITRATION_VIDEO_PRIORITY_5           (0x5 << 8)
#define PCIMEM_ARBITRATION_VIDEO_PRIORITY_6           (0x6 << 8)
#define PCIMEM_ARBITRATION_VIDEO_PRIORITY_7           (0x7 << 8)
#define PCIMEM_ARBITRATION_PANEL_MASK                 (0x7 << 4)
#define PCIMEM_ARBITRATION_PANEL_OFF                  (0x0 << 4)
#define PCIMEM_ARBITRATION_PANEL_PRIORITY_1           (0x1 << 4)
#define PCIMEM_ARBITRATION_PANEL_PRIORITY_2           (0x2 << 4)
#define PCIMEM_ARBITRATION_PANEL_PRIORITY_3           (0x3 << 4)
#define PCIMEM_ARBITRATION_PANEL_PRIORITY_4           (0x4 << 4)
#define PCIMEM_ARBITRATION_PANEL_PRIORITY_5           (0x5 << 4)
#define PCIMEM_ARBITRATION_PANEL_PRIORITY_6           (0x6 << 4)
#define PCIMEM_ARBITRATION_PANEL_PRIORITY_7           (0x7 << 4)
#define PCIMEM_ARBITRATION_CRT_MASK                   0x7
#define PCIMEM_ARBITRATION_CRT_OFF                    0x0
#define PCIMEM_ARBITRATION_CRT_PRIORITY_1             0x1
#define PCIMEM_ARBITRATION_CRT_PRIORITY_2             0x2
#define PCIMEM_ARBITRATION_CRT_PRIORITY_3             0x3
#define PCIMEM_ARBITRATION_CRT_PRIORITY_4             0x4
#define PCIMEM_ARBITRATION_CRT_PRIORITY_5             0x5
#define PCIMEM_ARBITRATION_CRT_PRIORITY_6             0x6
#define PCIMEM_ARBITRATION_CRT_PRIORITY_7             0x7

#define RAW_INT                                       0x000020
#define RAW_INT_ZVPORT1_VSYNC                         BIT(4)
#define RAW_INT_ZVPORT0_VSYNC                         BIT(3)
#define RAW_INT_CRT_VSYNC                             BIT(2)
#define RAW_INT_PANEL_VSYNC                           BIT(1)
#define RAW_INT_VGA_VSYNC                             BIT(0)

#define INT_STATUS                                    0x000024
#define INT_STATUS_GPIO31                             BIT(31)
#define INT_STATUS_GPIO30                             BIT(30)
#define INT_STATUS_GPIO29                             BIT(29)
#define INT_STATUS_GPIO28                             BIT(28)
#define INT_STATUS_GPIO27                             BIT(27)
#define INT_STATUS_GPIO26                             BIT(26)
#define INT_STATUS_GPIO25                             BIT(25)
#define INT_STATUS_I2C                                BIT(12)
#define INT_STATUS_PWM                                BIT(11)
#define INT_STATUS_DMA1                               BIT(10)
#define INT_STATUS_DMA0                               BIT(9)
#define INT_STATUS_PCI                                BIT(8)
#define INT_STATUS_SSP1                               BIT(7)
#define INT_STATUS_SSP0                               BIT(6)
#define INT_STATUS_DE                                 BIT(5)
#define INT_STATUS_ZVPORT1_VSYNC                      BIT(4)
#define INT_STATUS_ZVPORT0_VSYNC                      BIT(3)
#define INT_STATUS_CRT_VSYNC                          BIT(2)
#define INT_STATUS_PANEL_VSYNC                        BIT(1)
#define INT_STATUS_VGA_VSYNC                          BIT(0)

#define INT_MASK                                      0x000028
#define INT_MASK_GPIO31                               BIT(31)
#define INT_MASK_GPIO30                               BIT(30)
#define INT_MASK_GPIO29                               BIT(29)
#define INT_MASK_GPIO28                               BIT(28)
#define INT_MASK_GPIO27                               BIT(27)
#define INT_MASK_GPIO26                               BIT(26)
#define INT_MASK_GPIO25                               BIT(25)
#define INT_MASK_I2C                                  BIT(12)
#define INT_MASK_PWM                                  BIT(11)
#define INT_MASK_DMA1                                 BIT(10)
#define INT_MASK_DMA                                  BIT(9)
#define INT_MASK_PCI                                  BIT(8)
#define INT_MASK_SSP1                                 BIT(7)
#define INT_MASK_SSP0                                 BIT(6)
#define INT_MASK_DE                                   BIT(5)
#define INT_MASK_ZVPORT1_VSYNC                        BIT(4)
#define INT_MASK_ZVPORT0_VSYNC                        BIT(3)
#define INT_MASK_CRT_VSYNC                            BIT(2)
#define INT_MASK_PANEL_VSYNC                          BIT(1)
#define INT_MASK_VGA_VSYNC                            BIT(0)

#define CURRENT_GATE                                  0x000040
#define CURRENT_GATE_MCLK_MASK                        (0x3 << 14)
#ifdef VALIDATION_CHIP
    #define CURRENT_GATE_MCLK_112MHZ                  (0x0 << 14)
    #define CURRENT_GATE_MCLK_84MHZ                   (0x1 << 14)
    #define CURRENT_GATE_MCLK_56MHZ                   (0x2 << 14)
    #define CURRENT_GATE_MCLK_42MHZ                   (0x3 << 14)
#else
    #define CURRENT_GATE_MCLK_DIV_3                   (0x0 << 14)
    #define CURRENT_GATE_MCLK_DIV_4                   (0x1 << 14)
    #define CURRENT_GATE_MCLK_DIV_6                   (0x2 << 14)
    #define CURRENT_GATE_MCLK_DIV_8                   (0x3 << 14)
#endif
#define CURRENT_GATE_M2XCLK_MASK                      (0x3 << 12)
#ifdef VALIDATION_CHIP
    #define CURRENT_GATE_M2XCLK_336MHZ                (0x0 << 12)
    #define CURRENT_GATE_M2XCLK_168MHZ                (0x1 << 12)
    #define CURRENT_GATE_M2XCLK_112MHZ                (0x2 << 12)
    #define CURRENT_GATE_M2XCLK_84MHZ                 (0x3 << 12)
#else
    #define CURRENT_GATE_M2XCLK_DIV_1                 (0x0 << 12)
    #define CURRENT_GATE_M2XCLK_DIV_2                 (0x1 << 12)
    #define CURRENT_GATE_M2XCLK_DIV_3                 (0x2 << 12)
    #define CURRENT_GATE_M2XCLK_DIV_4                 (0x3 << 12)
#endif
#define CURRENT_GATE_VGA                              BIT(10)
#define CURRENT_GATE_PWM                              BIT(9)
#define CURRENT_GATE_I2C                              BIT(8)
#define CURRENT_GATE_SSP                              BIT(7)
#define CURRENT_GATE_GPIO                             BIT(6)
#define CURRENT_GATE_ZVPORT                           BIT(5)
#define CURRENT_GATE_CSC                              BIT(4)
#define CURRENT_GATE_DE                               BIT(3)
#define CURRENT_GATE_DISPLAY                          BIT(2)
#define CURRENT_GATE_LOCALMEM                         BIT(1)
#define CURRENT_GATE_DMA                              BIT(0)

#define MODE0_GATE                                    0x000044
#define MODE0_GATE_MCLK_MASK                          (0x3 << 14)
#define MODE0_GATE_MCLK_112MHZ                        (0x0 << 14)
#define MODE0_GATE_MCLK_84MHZ                         (0x1 << 14)
#define MODE0_GATE_MCLK_56MHZ                         (0x2 << 14)
#define MODE0_GATE_MCLK_42MHZ                         (0x3 << 14)
#define MODE0_GATE_M2XCLK_MASK                        (0x3 << 12)
#define MODE0_GATE_M2XCLK_336MHZ                      (0x0 << 12)
#define MODE0_GATE_M2XCLK_168MHZ                      (0x1 << 12)
#define MODE0_GATE_M2XCLK_112MHZ                      (0x2 << 12)
#define MODE0_GATE_M2XCLK_84MHZ                       (0x3 << 12)
#define MODE0_GATE_VGA                                BIT(10)
#define MODE0_GATE_PWM                                BIT(9)
#define MODE0_GATE_I2C                                BIT(8)
#define MODE0_GATE_SSP                                BIT(7)
#define MODE0_GATE_GPIO                               BIT(6)
#define MODE0_GATE_ZVPORT                             BIT(5)
#define MODE0_GATE_CSC                                BIT(4)
#define MODE0_GATE_DE                                 BIT(3)
#define MODE0_GATE_DISPLAY                            BIT(2)
#define MODE0_GATE_LOCALMEM                           BIT(1)
#define MODE0_GATE_DMA                                BIT(0)

#define MODE1_GATE                                    0x000048
#define MODE1_GATE_MCLK_MASK                          (0x3 << 14)
#define MODE1_GATE_MCLK_112MHZ                        (0x0 << 14)
#define MODE1_GATE_MCLK_84MHZ                         (0x1 << 14)
#define MODE1_GATE_MCLK_56MHZ                         (0x2 << 14)
#define MODE1_GATE_MCLK_42MHZ                         (0x3 << 14)
#define MODE1_GATE_M2XCLK_MASK                        (0x3 << 12)
#define MODE1_GATE_M2XCLK_336MHZ                      (0x0 << 12)
#define MODE1_GATE_M2XCLK_168MHZ                      (0x1 << 12)
#define MODE1_GATE_M2XCLK_112MHZ                      (0x2 << 12)
#define MODE1_GATE_M2XCLK_84MHZ                       (0x3 << 12)
#define MODE1_GATE_VGA                                BIT(10)
#define MODE1_GATE_PWM                                BIT(9)
#define MODE1_GATE_I2C                                BIT(8)
#define MODE1_GATE_SSP                                BIT(7)
#define MODE1_GATE_GPIO                               BIT(6)
#define MODE1_GATE_ZVPORT                             BIT(5)
#define MODE1_GATE_CSC                                BIT(4)
#define MODE1_GATE_DE                                 BIT(3)
#define MODE1_GATE_DISPLAY                            BIT(2)
#define MODE1_GATE_LOCALMEM                           BIT(1)
#define MODE1_GATE_DMA                                BIT(0)

#define POWER_MODE_CTRL                               0x00004C
#ifdef VALIDATION_CHIP
    #define POWER_MODE_CTRL_336CLK                    BIT(4)
#endif
#define POWER_MODE_CTRL_OSC_INPUT                     BIT(3)
#define POWER_MODE_CTRL_ACPI                          BIT(2)
#define POWER_MODE_CTRL_MODE_MASK                     (0x3 << 0)
#define POWER_MODE_CTRL_MODE_MODE0                    (0x0 << 0)
#define POWER_MODE_CTRL_MODE_MODE1                    (0x1 << 0)
#define POWER_MODE_CTRL_MODE_SLEEP                    (0x2 << 0)

#define PCI_MASTER_BASE                               0x000050
#define PCI_MASTER_BASE_ADDRESS_MASK                  0xff

#define DEVICE_ID                                     0x000054
#define DEVICE_ID_DEVICE_ID_MASK                      (0xffff << 16)
#define DEVICE_ID_REVISION_ID_MASK                    0xff

#define PLL_CLK_COUNT                                 0x000058
#define PLL_CLK_COUNT_COUNTER_MASK                    0xffff

#define PANEL_PLL_CTRL                                0x00005C
#define PLL_CTRL_BYPASS                               BIT(18)
#define PLL_CTRL_POWER                                BIT(17)
#define PLL_CTRL_INPUT                                BIT(16)
#ifdef VALIDATION_CHIP
    #define PLL_CTRL_OD_SHIFT                         14
    #define PLL_CTRL_OD_MASK                          (0x3 << 14)
#else
    #define PLL_CTRL_POD_SHIFT                        14
    #define PLL_CTRL_POD_MASK                         (0x3 << 14)
    #define PLL_CTRL_OD_SHIFT                         12
    #define PLL_CTRL_OD_MASK                          (0x3 << 12)
#endif
#define PLL_CTRL_N_SHIFT                              8
#define PLL_CTRL_N_MASK                               (0xf << 8)
#define PLL_CTRL_M_SHIFT                              0
#define PLL_CTRL_M_MASK                               0xff

#define CRT_PLL_CTRL                                  0x000060

#define VGA_PLL0_CTRL                                 0x000064

#define VGA_PLL1_CTRL                                 0x000068

#define SCRATCH_DATA                                  0x00006c

#ifndef VALIDATION_CHIP

#define MXCLK_PLL_CTRL                                0x000070

#define VGA_CONFIGURATION                             0x000088
#define VGA_CONFIGURATION_USER_DEFINE_MASK            (0x3 << 4)
#define VGA_CONFIGURATION_PLL                         BIT(2)
#define VGA_CONFIGURATION_MODE                        BIT(1)

#endif

#define GPIO_DATA                                       0x010000
#define GPIO_DATA_31                                    BIT(31)
#define GPIO_DATA_30                                    BIT(30)
#define GPIO_DATA_29                                    BIT(29)
#define GPIO_DATA_28                                    BIT(28)
#define GPIO_DATA_27                                    BIT(27)
#define GPIO_DATA_26                                    BIT(26)
#define GPIO_DATA_25                                    BIT(25)
#define GPIO_DATA_24                                    BIT(24)
#define GPIO_DATA_23                                    BIT(23)
#define GPIO_DATA_22                                    BIT(22)
#define GPIO_DATA_21                                    BIT(21)
#define GPIO_DATA_20                                    BIT(20)
#define GPIO_DATA_19                                    BIT(19)
#define GPIO_DATA_18                                    BIT(18)
#define GPIO_DATA_17                                    BIT(17)
#define GPIO_DATA_16                                    BIT(16)
#define GPIO_DATA_15                                    BIT(15)
#define GPIO_DATA_14                                    BIT(14)
#define GPIO_DATA_13                                    BIT(13)
#define GPIO_DATA_12                                    BIT(12)
#define GPIO_DATA_11                                    BIT(11)
#define GPIO_DATA_10                                    BIT(10)
#define GPIO_DATA_9                                     BIT(9)
#define GPIO_DATA_8                                     BIT(8)
#define GPIO_DATA_7                                     BIT(7)
#define GPIO_DATA_6                                     BIT(6)
#define GPIO_DATA_5                                     BIT(5)
#define GPIO_DATA_4                                     BIT(4)
#define GPIO_DATA_3                                     BIT(3)
#define GPIO_DATA_2                                     BIT(2)
#define GPIO_DATA_1                                     BIT(1)
#define GPIO_DATA_0                                     BIT(0)

#define GPIO_DATA_DIRECTION                             0x010004
#define GPIO_DATA_DIRECTION_31                          BIT(31)
#define GPIO_DATA_DIRECTION_30                          BIT(30)
#define GPIO_DATA_DIRECTION_29                          BIT(29)
#define GPIO_DATA_DIRECTION_28                          BIT(28)
#define GPIO_DATA_DIRECTION_27                          BIT(27)
#define GPIO_DATA_DIRECTION_26                          BIT(26)
#define GPIO_DATA_DIRECTION_25                          BIT(25)
#define GPIO_DATA_DIRECTION_24                          BIT(24)
#define GPIO_DATA_DIRECTION_23                          BIT(23)
#define GPIO_DATA_DIRECTION_22                          BIT(22)
#define GPIO_DATA_DIRECTION_21                          BIT(21)
#define GPIO_DATA_DIRECTION_20                          BIT(20)
#define GPIO_DATA_DIRECTION_19                          BIT(19)
#define GPIO_DATA_DIRECTION_18                          BIT(18)
#define GPIO_DATA_DIRECTION_17                          BIT(17)
#define GPIO_DATA_DIRECTION_16                          BIT(16)
#define GPIO_DATA_DIRECTION_15                          BIT(15)
#define GPIO_DATA_DIRECTION_14                          BIT(14)
#define GPIO_DATA_DIRECTION_13                          BIT(13)
#define GPIO_DATA_DIRECTION_12                          BIT(12)
#define GPIO_DATA_DIRECTION_11                          BIT(11)
#define GPIO_DATA_DIRECTION_10                          BIT(10)
#define GPIO_DATA_DIRECTION_9                           BIT(9)
#define GPIO_DATA_DIRECTION_8                           BIT(8)
#define GPIO_DATA_DIRECTION_7                           BIT(7)
#define GPIO_DATA_DIRECTION_6                           BIT(6)
#define GPIO_DATA_DIRECTION_5                           BIT(5)
#define GPIO_DATA_DIRECTION_4                           BIT(4)
#define GPIO_DATA_DIRECTION_3                           BIT(3)
#define GPIO_DATA_DIRECTION_2                           BIT(2)
#define GPIO_DATA_DIRECTION_1                           BIT(1)
#define GPIO_DATA_DIRECTION_0                           BIT(0)

#define GPIO_INTERRUPT_SETUP                            0x010008
#define GPIO_INTERRUPT_SETUP_TRIGGER_31                 BIT(22)
#define GPIO_INTERRUPT_SETUP_TRIGGER_30                 BIT(21)
#define GPIO_INTERRUPT_SETUP_TRIGGER_29                 BIT(20)
#define GPIO_INTERRUPT_SETUP_TRIGGER_28                 BIT(19)
#define GPIO_INTERRUPT_SETUP_TRIGGER_27                 BIT(18)
#define GPIO_INTERRUPT_SETUP_TRIGGER_26                 BIT(17)
#define GPIO_INTERRUPT_SETUP_TRIGGER_25                 BIT(16)
#define GPIO_INTERRUPT_SETUP_ACTIVE_31                  BIT(14)
#define GPIO_INTERRUPT_SETUP_ACTIVE_30                  BIT(13)
#define GPIO_INTERRUPT_SETUP_ACTIVE_29                  BIT(12)
#define GPIO_INTERRUPT_SETUP_ACTIVE_28                  BIT(11)
#define GPIO_INTERRUPT_SETUP_ACTIVE_27                  BIT(10)
#define GPIO_INTERRUPT_SETUP_ACTIVE_26                  BIT(9)
#define GPIO_INTERRUPT_SETUP_ACTIVE_25                  BIT(8)
#define GPIO_INTERRUPT_SETUP_ENABLE_31                  BIT(6)
#define GPIO_INTERRUPT_SETUP_ENABLE_30                  BIT(5)
#define GPIO_INTERRUPT_SETUP_ENABLE_29                  BIT(4)
#define GPIO_INTERRUPT_SETUP_ENABLE_28                  BIT(3)
#define GPIO_INTERRUPT_SETUP_ENABLE_27                  BIT(2)
#define GPIO_INTERRUPT_SETUP_ENABLE_26                  BIT(1)
#define GPIO_INTERRUPT_SETUP_ENABLE_25                  BIT(0)

#define GPIO_INTERRUPT_STATUS                           0x01000C
#define GPIO_INTERRUPT_STATUS_31                        BIT(22)
#define GPIO_INTERRUPT_STATUS_30                        BIT(21)
#define GPIO_INTERRUPT_STATUS_29                        BIT(20)
#define GPIO_INTERRUPT_STATUS_28                        BIT(19)
#define GPIO_INTERRUPT_STATUS_27                        BIT(18)
#define GPIO_INTERRUPT_STATUS_26                        BIT(17)
#define GPIO_INTERRUPT_STATUS_25                        BIT(16)


#define PANEL_DISPLAY_CTRL                            0x080000
#define PANEL_DISPLAY_CTRL_RESERVED_MASK              0xc0f08000
#define PANEL_DISPLAY_CTRL_SELECT_SHIFT               28
#define PANEL_DISPLAY_CTRL_SELECT_MASK                (0x3 << 28)
#define PANEL_DISPLAY_CTRL_SELECT_PANEL               (0x0 << 28)
#define PANEL_DISPLAY_CTRL_SELECT_VGA                 (0x1 << 28)
#define PANEL_DISPLAY_CTRL_SELECT_CRT                 (0x2 << 28)
#define PANEL_DISPLAY_CTRL_FPEN                       BIT(27)
#define PANEL_DISPLAY_CTRL_VBIASEN                    BIT(26)
#define PANEL_DISPLAY_CTRL_DATA                       BIT(25)
#define PANEL_DISPLAY_CTRL_FPVDDEN                    BIT(24)
#define PANEL_DISPLAY_CTRL_DUAL_DISPLAY               BIT(19)
#define PANEL_DISPLAY_CTRL_DOUBLE_PIXEL               BIT(18)
#define PANEL_DISPLAY_CTRL_FIFO                       (0x3 << 16)
#define PANEL_DISPLAY_CTRL_FIFO_1                     (0x0 << 16)
#define PANEL_DISPLAY_CTRL_FIFO_3                     (0x1 << 16)
#define PANEL_DISPLAY_CTRL_FIFO_7                     (0x2 << 16)
#define PANEL_DISPLAY_CTRL_FIFO_11                    (0x3 << 16)
#define DISPLAY_CTRL_CLOCK_PHASE                      BIT(14)
#define DISPLAY_CTRL_VSYNC_PHASE                      BIT(13)
#define DISPLAY_CTRL_HSYNC_PHASE                      BIT(12)
#define PANEL_DISPLAY_CTRL_VSYNC                      BIT(11)
#define PANEL_DISPLAY_CTRL_CAPTURE_TIMING             BIT(10)
#define PANEL_DISPLAY_CTRL_COLOR_KEY                  BIT(9)
#define DISPLAY_CTRL_TIMING                           BIT(8)
#define PANEL_DISPLAY_CTRL_VERTICAL_PAN_DIR           BIT(7)
#define PANEL_DISPLAY_CTRL_VERTICAL_PAN               BIT(6)
#define PANEL_DISPLAY_CTRL_HORIZONTAL_PAN_DIR         BIT(5)
#define PANEL_DISPLAY_CTRL_HORIZONTAL_PAN             BIT(4)
#define DISPLAY_CTRL_GAMMA                            BIT(3)
#define DISPLAY_CTRL_PLANE                            BIT(2)
#define PANEL_DISPLAY_CTRL_FORMAT                     (0x3 << 0)
#define PANEL_DISPLAY_CTRL_FORMAT_8                   (0x0 << 0)
#define PANEL_DISPLAY_CTRL_FORMAT_16                  (0x1 << 0)
#define PANEL_DISPLAY_CTRL_FORMAT_32                  (0x2 << 0)

#define PANEL_PAN_CTRL                                0x080004
#define PANEL_PAN_CTRL_VERTICAL_PAN_MASK              (0xff << 24)
#define PANEL_PAN_CTRL_VERTICAL_VSYNC_MASK            (0x3f << 16)
#define PANEL_PAN_CTRL_HORIZONTAL_PAN_MASK            (0xff << 8)
#define PANEL_PAN_CTRL_HORIZONTAL_VSYNC_MASK          0x3f

#define PANEL_COLOR_KEY                               0x080008
#define PANEL_COLOR_KEY_MASK_MASK                     (0xffff << 16)
#define PANEL_COLOR_KEY_VALUE_MASK                    0xffff

#define PANEL_FB_ADDRESS                              0x08000C
#define PANEL_FB_ADDRESS_STATUS                       BIT(31)
#define PANEL_FB_ADDRESS_EXT                          BIT(27)
#define PANEL_FB_ADDRESS_ADDRESS_MASK                 0x1ffffff

#define PANEL_FB_WIDTH                                0x080010
#define PANEL_FB_WIDTH_WIDTH_SHIFT                    16
#define PANEL_FB_WIDTH_WIDTH_MASK                     (0x3fff << 16)
#define PANEL_FB_WIDTH_OFFSET_MASK                    0x3fff

#define PANEL_WINDOW_WIDTH                            0x080014
#define PANEL_WINDOW_WIDTH_WIDTH_SHIFT                16
#define PANEL_WINDOW_WIDTH_WIDTH_MASK                 (0xfff << 16)
#define PANEL_WINDOW_WIDTH_X_MASK                     0xfff

#define PANEL_WINDOW_HEIGHT                           0x080018
#define PANEL_WINDOW_HEIGHT_HEIGHT_SHIFT              16
#define PANEL_WINDOW_HEIGHT_HEIGHT_MASK               (0xfff << 16)
#define PANEL_WINDOW_HEIGHT_Y_MASK                    0xfff

#define PANEL_PLANE_TL                                0x08001C
#define PANEL_PLANE_TL_TOP_SHIFT                      16
#define PANEL_PLANE_TL_TOP_MASK                       (0xeff << 16)
#define PANEL_PLANE_TL_LEFT_MASK                      0xeff

#define PANEL_PLANE_BR                                0x080020
#define PANEL_PLANE_BR_BOTTOM_SHIFT                   16
#define PANEL_PLANE_BR_BOTTOM_MASK                    (0xeff << 16)
#define PANEL_PLANE_BR_RIGHT_MASK                     0xeff

#define PANEL_HORIZONTAL_TOTAL                        0x080024
#define PANEL_HORIZONTAL_TOTAL_TOTAL_SHIFT            16
#define PANEL_HORIZONTAL_TOTAL_TOTAL_MASK             (0xfff << 16)
#define PANEL_HORIZONTAL_TOTAL_DISPLAY_END_MASK       0xfff

#define PANEL_HORIZONTAL_SYNC                         0x080028
#define PANEL_HORIZONTAL_SYNC_WIDTH_SHIFT             16
#define PANEL_HORIZONTAL_SYNC_WIDTH_MASK              (0xff << 16)
#define PANEL_HORIZONTAL_SYNC_START_MASK              0xfff

#define PANEL_VERTICAL_TOTAL                          0x08002C
#define PANEL_VERTICAL_TOTAL_TOTAL_SHIFT              16
#define PANEL_VERTICAL_TOTAL_TOTAL_MASK               (0x7ff << 16)
#define PANEL_VERTICAL_TOTAL_DISPLAY_END_MASK         0x7ff

#define PANEL_VERTICAL_SYNC                           0x080030
#define PANEL_VERTICAL_SYNC_HEIGHT_SHIFT              16
#define PANEL_VERTICAL_SYNC_HEIGHT_MASK               (0x3f << 16)
#define PANEL_VERTICAL_SYNC_START_MASK                0x7ff

#define PANEL_CURRENT_LINE                            0x080034
#define PANEL_CURRENT_LINE_LINE_MASK                  0x7ff

/* Video Control */

#define VIDEO_DISPLAY_CTRL                              0x080040
#define VIDEO_DISPLAY_CTRL_LINE_BUFFER                  BIT(18)
#define VIDEO_DISPLAY_CTRL_FIFO_MASK                    (0x3 << 16)
#define VIDEO_DISPLAY_CTRL_FIFO_1                       (0x0 << 16)
#define VIDEO_DISPLAY_CTRL_FIFO_3                       (0x1 << 16)
#define VIDEO_DISPLAY_CTRL_FIFO_7                       (0x2 << 16)
#define VIDEO_DISPLAY_CTRL_FIFO_11                      (0x3 << 16)
#define VIDEO_DISPLAY_CTRL_BUFFER                       BIT(15)
#define VIDEO_DISPLAY_CTRL_CAPTURE                      BIT(14)
#define VIDEO_DISPLAY_CTRL_DOUBLE_BUFFER                BIT(13)
#define VIDEO_DISPLAY_CTRL_BYTE_SWAP                    BIT(12)
#define VIDEO_DISPLAY_CTRL_VERTICAL_SCALE               BIT(11)
#define VIDEO_DISPLAY_CTRL_HORIZONTAL_SCALE             BIT(10)
#define VIDEO_DISPLAY_CTRL_VERTICAL_MODE                BIT(9)
#define VIDEO_DISPLAY_CTRL_HORIZONTAL_MODE              BIT(8)
#define VIDEO_DISPLAY_CTRL_PIXEL_MASK                   (0xf << 4)
#define VIDEO_DISPLAY_CTRL_GAMMA                        BIT(3)
#define VIDEO_DISPLAY_CTRL_FORMAT_MASK                  0x3
#define VIDEO_DISPLAY_CTRL_FORMAT_8                     0x0
#define VIDEO_DISPLAY_CTRL_FORMAT_16                    0x1
#define VIDEO_DISPLAY_CTRL_FORMAT_32                    0x2
#define VIDEO_DISPLAY_CTRL_FORMAT_YUV                   0x3

#define VIDEO_FB_0_ADDRESS                            0x080044
#define VIDEO_FB_0_ADDRESS_STATUS                     BIT(31)
#define VIDEO_FB_0_ADDRESS_EXT                        BIT(27)
#define VIDEO_FB_0_ADDRESS_ADDRESS_MASK               0x3ffffff

#define VIDEO_FB_WIDTH                                0x080048
#define VIDEO_FB_WIDTH_WIDTH_MASK                     (0x3fff << 16)
#define VIDEO_FB_WIDTH_OFFSET_MASK                    0x3fff

#define VIDEO_FB_0_LAST_ADDRESS                       0x08004C
#define VIDEO_FB_0_LAST_ADDRESS_EXT                   BIT(27)
#define VIDEO_FB_0_LAST_ADDRESS_ADDRESS_MASK          0x3ffffff

#define VIDEO_PLANE_TL                                0x080050
#define VIDEO_PLANE_TL_TOP_MASK                       (0x7ff << 16)
#define VIDEO_PLANE_TL_LEFT_MASK                      0x7ff

#define VIDEO_PLANE_BR                                0x080054
#define VIDEO_PLANE_BR_BOTTOM_MASK                    (0x7ff << 16)
#define VIDEO_PLANE_BR_RIGHT_MASK                     0x7ff

#define VIDEO_SCALE                                   0x080058
#define VIDEO_SCALE_VERTICAL_MODE                     BIT(31)
#define VIDEO_SCALE_VERTICAL_SCALE_MASK               (0xfff << 16)
#define VIDEO_SCALE_HORIZONTAL_MODE                   BIT(15)
#define VIDEO_SCALE_HORIZONTAL_SCALE_MASK             0xfff

#define VIDEO_INITIAL_SCALE                           0x08005C
#define VIDEO_INITIAL_SCALE_FB_1_MASK                 (0xfff << 16)
#define VIDEO_INITIAL_SCALE_FB_0_MASK                 0xfff

#define VIDEO_YUV_CONSTANTS                           0x080060
#define VIDEO_YUV_CONSTANTS_Y_MASK                    (0xff << 24)
#define VIDEO_YUV_CONSTANTS_R_MASK                    (0xff << 16)
#define VIDEO_YUV_CONSTANTS_G_MASK                    (0xff << 8)
#define VIDEO_YUV_CONSTANTS_B_MASK                    0xff

#define VIDEO_FB_1_ADDRESS                            0x080064
#define VIDEO_FB_1_ADDRESS_STATUS                     BIT(31)
#define VIDEO_FB_1_ADDRESS_EXT                        BIT(27)
#define VIDEO_FB_1_ADDRESS_ADDRESS_MASK               0x3ffffff

#define VIDEO_FB_1_LAST_ADDRESS                       0x080068
#define VIDEO_FB_1_LAST_ADDRESS_EXT                   BIT(27)
#define VIDEO_FB_1_LAST_ADDRESS_ADDRESS_MASK          0x3ffffff

/* Video Alpha Control */

#define VIDEO_ALPHA_DISPLAY_CTRL                        0x080080
#define VIDEO_ALPHA_DISPLAY_CTRL_SELECT                 BIT(28)
#define VIDEO_ALPHA_DISPLAY_CTRL_ALPHA_MASK             (0xf << 24)
#define VIDEO_ALPHA_DISPLAY_CTRL_FIFO_MASK              (0x3 << 16)
#define VIDEO_ALPHA_DISPLAY_CTRL_FIFO_1                 (0x0 << 16)
#define VIDEO_ALPHA_DISPLAY_CTRL_FIFO_3                 (0x1 << 16)
#define VIDEO_ALPHA_DISPLAY_CTRL_FIFO_7                 (0x2 << 16)
#define VIDEO_ALPHA_DISPLAY_CTRL_FIFO_11                (0x3 << 16)
#define VIDEO_ALPHA_DISPLAY_CTRL_VERT_SCALE             BIT(11)
#define VIDEO_ALPHA_DISPLAY_CTRL_HORZ_SCALE             BIT(10)
#define VIDEO_ALPHA_DISPLAY_CTRL_VERT_MODE              BIT(9)
#define VIDEO_ALPHA_DISPLAY_CTRL_HORZ_MODE              BIT(8)
#define VIDEO_ALPHA_DISPLAY_CTRL_PIXEL_MASK             (0xf << 4)
#define VIDEO_ALPHA_DISPLAY_CTRL_CHROMA_KEY             BIT(3)
#define VIDEO_ALPHA_DISPLAY_CTRL_FORMAT_MASK            0x3
#define VIDEO_ALPHA_DISPLAY_CTRL_FORMAT_8               0x0
#define VIDEO_ALPHA_DISPLAY_CTRL_FORMAT_16              0x1
#define VIDEO_ALPHA_DISPLAY_CTRL_FORMAT_ALPHA_4_4       0x2
#define VIDEO_ALPHA_DISPLAY_CTRL_FORMAT_ALPHA_4_4_4_4   0x3

#define VIDEO_ALPHA_FB_ADDRESS                        0x080084
#define VIDEO_ALPHA_FB_ADDRESS_STATUS                 BIT(31)
#define VIDEO_ALPHA_FB_ADDRESS_EXT                    BIT(27)
#define VIDEO_ALPHA_FB_ADDRESS_ADDRESS_MASK           0x3ffffff

#define VIDEO_ALPHA_FB_WIDTH                          0x080088
#define VIDEO_ALPHA_FB_WIDTH_WIDTH_MASK               (0x3fff << 16)
#define VIDEO_ALPHA_FB_WIDTH_OFFSET_MASK              0x3fff

#define VIDEO_ALPHA_FB_LAST_ADDRESS                   0x08008C
#define VIDEO_ALPHA_FB_LAST_ADDRESS_EXT               BIT(27)
#define VIDEO_ALPHA_FB_LAST_ADDRESS_ADDRESS_MASK      0x3ffffff

#define VIDEO_ALPHA_PLANE_TL                          0x080090
#define VIDEO_ALPHA_PLANE_TL_TOP_MASK                 (0x7ff << 16)
#define VIDEO_ALPHA_PLANE_TL_LEFT_MASK                0x7ff

#define VIDEO_ALPHA_PLANE_BR                          0x080094
#define VIDEO_ALPHA_PLANE_BR_BOTTOM_MASK              (0x7ff << 16)
#define VIDEO_ALPHA_PLANE_BR_RIGHT_MASK               0x7ff

#define VIDEO_ALPHA_SCALE                             0x080098
#define VIDEO_ALPHA_SCALE_VERTICAL_MODE               BIT(31)
#define VIDEO_ALPHA_SCALE_VERTICAL_SCALE_MASK         (0xfff << 16)
#define VIDEO_ALPHA_SCALE_HORIZONTAL_MODE             BIT(15)
#define VIDEO_ALPHA_SCALE_HORIZONTAL_SCALE_MASK       0xfff

#define VIDEO_ALPHA_INITIAL_SCALE                     0x08009C
#define VIDEO_ALPHA_INITIAL_SCALE_VERTICAL_MASK       (0xfff << 16)
#define VIDEO_ALPHA_INITIAL_SCALE_HORIZONTAL_MASK     0xfff

#define VIDEO_ALPHA_CHROMA_KEY                        0x0800A0
#define VIDEO_ALPHA_CHROMA_KEY_MASK_MASK              (0xffff << 16)
#define VIDEO_ALPHA_CHROMA_KEY_VALUE_MASK             0xffff

#define VIDEO_ALPHA_COLOR_LOOKUP_01                   0x0800A4
#define VIDEO_ALPHA_COLOR_LOOKUP_01_1_MASK            (0xffff << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_01_1_RED_MASK        (0x1f << 27)
#define VIDEO_ALPHA_COLOR_LOOKUP_01_1_GREEN_MASK      (0x3f << 21)
#define VIDEO_ALPHA_COLOR_LOOKUP_01_1_BLUE_MASK       (0x1f << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_01_0_MASK            0xffff
#define VIDEO_ALPHA_COLOR_LOOKUP_01_0_RED_MASK        (0x1f << 11)
#define VIDEO_ALPHA_COLOR_LOOKUP_01_0_GREEN_MASK      (0x3f << 5)
#define VIDEO_ALPHA_COLOR_LOOKUP_01_0_BLUE_MASK       0x1f

#define VIDEO_ALPHA_COLOR_LOOKUP_23                   0x0800A8
#define VIDEO_ALPHA_COLOR_LOOKUP_23_3_MASK            (0xffff << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_23_3_RED_MASK        (0x1f << 27)
#define VIDEO_ALPHA_COLOR_LOOKUP_23_3_GREEN_MASK      (0x3f << 21)
#define VIDEO_ALPHA_COLOR_LOOKUP_23_3_BLUE_MASK       (0x1f << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_23_2_MASK            0xffff
#define VIDEO_ALPHA_COLOR_LOOKUP_23_2_RED_MASK        (0x1f << 11)
#define VIDEO_ALPHA_COLOR_LOOKUP_23_2_GREEN_MASK      (0x3f << 5)
#define VIDEO_ALPHA_COLOR_LOOKUP_23_2_BLUE_MASK       0x1f

#define VIDEO_ALPHA_COLOR_LOOKUP_45                   0x0800AC
#define VIDEO_ALPHA_COLOR_LOOKUP_45_5_MASK            (0xffff << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_45_5_RED_MASK        (0x1f << 27)
#define VIDEO_ALPHA_COLOR_LOOKUP_45_5_GREEN_MASK      (0x3f << 21)
#define VIDEO_ALPHA_COLOR_LOOKUP_45_5_BLUE_MASK       (0x1f << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_45_4_MASK            0xffff
#define VIDEO_ALPHA_COLOR_LOOKUP_45_4_RED_MASK        (0x1f << 11)
#define VIDEO_ALPHA_COLOR_LOOKUP_45_4_GREEN_MASK      (0x3f << 5)
#define VIDEO_ALPHA_COLOR_LOOKUP_45_4_BLUE_MASK       0x1f

#define VIDEO_ALPHA_COLOR_LOOKUP_67                   0x0800B0
#define VIDEO_ALPHA_COLOR_LOOKUP_67_7_MASK            (0xffff << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_67_7_RED_MASK        (0x1f << 27)
#define VIDEO_ALPHA_COLOR_LOOKUP_67_7_GREEN_MASK      (0x3f << 21)
#define VIDEO_ALPHA_COLOR_LOOKUP_67_7_BLUE_MASK       (0x1f << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_67_6_MASK            0xffff
#define VIDEO_ALPHA_COLOR_LOOKUP_67_6_RED_MASK        (0x1f << 11)
#define VIDEO_ALPHA_COLOR_LOOKUP_67_6_GREEN_MASK      (0x3f << 5)
#define VIDEO_ALPHA_COLOR_LOOKUP_67_6_BLUE_MASK       0x1f

#define VIDEO_ALPHA_COLOR_LOOKUP_89                   0x0800B4
#define VIDEO_ALPHA_COLOR_LOOKUP_89_9_MASK            (0xffff << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_89_9_RED_MASK        (0x1f << 27)
#define VIDEO_ALPHA_COLOR_LOOKUP_89_9_GREEN_MASK      (0x3f << 21)
#define VIDEO_ALPHA_COLOR_LOOKUP_89_9_BLUE_MASK       (0x1f << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_89_8_MASK            0xffff
#define VIDEO_ALPHA_COLOR_LOOKUP_89_8_RED_MASK        (0x1f << 11)
#define VIDEO_ALPHA_COLOR_LOOKUP_89_8_GREEN_MASK      (0x3f << 5)
#define VIDEO_ALPHA_COLOR_LOOKUP_89_8_BLUE_MASK       0x1f

#define VIDEO_ALPHA_COLOR_LOOKUP_AB                   0x0800B8
#define VIDEO_ALPHA_COLOR_LOOKUP_AB_B_MASK            (0xffff << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_AB_B_RED_MASK        (0x1f << 27)
#define VIDEO_ALPHA_COLOR_LOOKUP_AB_B_GREEN_MASK      (0x3f << 21)
#define VIDEO_ALPHA_COLOR_LOOKUP_AB_B_BLUE_MASK       (0x1f << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_AB_A_MASK            0xffff
#define VIDEO_ALPHA_COLOR_LOOKUP_AB_A_RED_MASK        (0x1f << 11)
#define VIDEO_ALPHA_COLOR_LOOKUP_AB_A_GREEN_MASK      (0x3f << 5)
#define VIDEO_ALPHA_COLOR_LOOKUP_AB_A_BLUE_MASK       0x1f

#define VIDEO_ALPHA_COLOR_LOOKUP_CD                   0x0800BC
#define VIDEO_ALPHA_COLOR_LOOKUP_CD_D_MASK            (0xffff << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_CD_D_RED_MASK        (0x1f << 27)
#define VIDEO_ALPHA_COLOR_LOOKUP_CD_D_GREEN_MASK      (0x3f << 21)
#define VIDEO_ALPHA_COLOR_LOOKUP_CD_D_BLUE_MASK       (0x1f << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_CD_C_MASK            0xffff
#define VIDEO_ALPHA_COLOR_LOOKUP_CD_C_RED_MASK        (0x1f << 11)
#define VIDEO_ALPHA_COLOR_LOOKUP_CD_C_GREEN_MASK      (0x3f << 5)
#define VIDEO_ALPHA_COLOR_LOOKUP_CD_C_BLUE_MASK       0x1f

#define VIDEO_ALPHA_COLOR_LOOKUP_EF                   0x0800C0
#define VIDEO_ALPHA_COLOR_LOOKUP_EF_F_MASK            (0xffff << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_EF_F_RED_MASK        (0x1f << 27)
#define VIDEO_ALPHA_COLOR_LOOKUP_EF_F_GREEN_MASK      (0x3f << 21)
#define VIDEO_ALPHA_COLOR_LOOKUP_EF_F_BLUE_MASK       (0x1f << 16)
#define VIDEO_ALPHA_COLOR_LOOKUP_EF_E_MASK            0xffff
#define VIDEO_ALPHA_COLOR_LOOKUP_EF_E_RED_MASK        (0x1f << 11)
#define VIDEO_ALPHA_COLOR_LOOKUP_EF_E_GREEN_MASK      (0x3f << 5)
#define VIDEO_ALPHA_COLOR_LOOKUP_EF_E_BLUE_MASK       0x1f

/* Panel Cursor Control */

#define PANEL_HWC_ADDRESS                             0x0800F0
#define PANEL_HWC_ADDRESS_ENABLE                      BIT(31)
#define PANEL_HWC_ADDRESS_EXT                         BIT(27)
#define PANEL_HWC_ADDRESS_ADDRESS_MASK                0x3ffffff

#define PANEL_HWC_LOCATION                            0x0800F4
#define PANEL_HWC_LOCATION_TOP                        BIT(27)
#define PANEL_HWC_LOCATION_Y_MASK                     (0x7ff << 16)
#define PANEL_HWC_LOCATION_LEFT                       BIT(11)
#define PANEL_HWC_LOCATION_X_MASK                     0x7ff

#define PANEL_HWC_COLOR_12                            0x0800F8
#define PANEL_HWC_COLOR_12_2_RGB565_MASK              (0xffff << 16)
#define PANEL_HWC_COLOR_12_1_RGB565_MASK              0xffff

#define PANEL_HWC_COLOR_3                             0x0800FC
#define PANEL_HWC_COLOR_3_RGB565_MASK                 0xffff

/* Old Definitions +++ */
#define PANEL_HWC_COLOR_01                            0x0800F8
#define PANEL_HWC_COLOR_01_1_RED_MASK                 (0x1f << 27)
#define PANEL_HWC_COLOR_01_1_GREEN_MASK               (0x3f << 21)
#define PANEL_HWC_COLOR_01_1_BLUE_MASK                (0x1f << 16)
#define PANEL_HWC_COLOR_01_0_RED_MASK                 (0x1f << 11)
#define PANEL_HWC_COLOR_01_0_GREEN_MASK               (0x3f << 5)
#define PANEL_HWC_COLOR_01_0_BLUE_MASK                0x1f

#define PANEL_HWC_COLOR_2                             0x0800FC
#define PANEL_HWC_COLOR_2_RED_MASK                    (0x1f << 11)
#define PANEL_HWC_COLOR_2_GREEN_MASK                  (0x3f << 5)
#define PANEL_HWC_COLOR_2_BLUE_MASK                   0x1f
/* Old Definitions --- */

/* Alpha Control */

#define ALPHA_DISPLAY_CTRL                            0x080100
#define ALPHA_DISPLAY_CTRL_SELECT                     BIT(28)
#define ALPHA_DISPLAY_CTRL_ALPHA_MASK                 (0xf << 24)
#define ALPHA_DISPLAY_CTRL_FIFO_MASK                  (0x3 << 16)
#define ALPHA_DISPLAY_CTRL_FIFO_1                     (0x0 << 16)
#define ALPHA_DISPLAY_CTRL_FIFO_3                     (0x1 << 16)
#define ALPHA_DISPLAY_CTRL_FIFO_7                     (0x2 << 16)
#define ALPHA_DISPLAY_CTRL_FIFO_11                    (0x3 << 16)
#define ALPHA_DISPLAY_CTRL_PIXEL_MASK                 (0xf << 4)
#define ALPHA_DISPLAY_CTRL_CHROMA_KEY                 BIT(3)
#define ALPHA_DISPLAY_CTRL_FORMAT_MASK                0x3
#define ALPHA_DISPLAY_CTRL_FORMAT_16                  0x1
#define ALPHA_DISPLAY_CTRL_FORMAT_ALPHA_4_4           0x2
#define ALPHA_DISPLAY_CTRL_FORMAT_ALPHA_4_4_4_4       0x3

#define ALPHA_FB_ADDRESS                              0x080104
#define ALPHA_FB_ADDRESS_STATUS                       BIT(31)
#define ALPHA_FB_ADDRESS_EXT                          BIT(27)
#define ALPHA_FB_ADDRESS_ADDRESS_MASK                 0x3ffffff

#define ALPHA_FB_WIDTH                                0x080108
#define ALPHA_FB_WIDTH_WIDTH_MASK                     (0x3fff << 16)
#define ALPHA_FB_WIDTH_OFFSET_MASK                    0x3fff

#define ALPHA_PLANE_TL                                0x08010C
#define ALPHA_PLANE_TL_TOP_MASK                       (0x7ff << 16)
#define ALPHA_PLANE_TL_LEFT_MASK                      0x7ff

#define ALPHA_PLANE_BR                                0x080110
#define ALPHA_PLANE_BR_BOTTOM_MASK                    (0x7ff << 16)
#define ALPHA_PLANE_BR_RIGHT_MASK                     0x7ff

#define ALPHA_CHROMA_KEY                              0x080114
#define ALPHA_CHROMA_KEY_MASK_MASK                    (0xffff << 16)
#define ALPHA_CHROMA_KEY_VALUE_MASK                   0xffff

#define ALPHA_COLOR_LOOKUP_01                         0x080118
#define ALPHA_COLOR_LOOKUP_01_1_MASK                  (0xffff << 16)
#define ALPHA_COLOR_LOOKUP_01_1_RED_MASK              (0x1f << 27)
#define ALPHA_COLOR_LOOKUP_01_1_GREEN_MASK            (0x3f << 21)
#define ALPHA_COLOR_LOOKUP_01_1_BLUE_MASK             (0x1f << 16)
#define ALPHA_COLOR_LOOKUP_01_0_MASK                  0xffff
#define ALPHA_COLOR_LOOKUP_01_0_RED_MASK              (0x1f << 11)
#define ALPHA_COLOR_LOOKUP_01_0_GREEN_MASK            (0x3f << 5)
#define ALPHA_COLOR_LOOKUP_01_0_BLUE_MASK             0x1f

#define ALPHA_COLOR_LOOKUP_23                         0x08011C
#define ALPHA_COLOR_LOOKUP_23_3_MASK                  (0xffff << 16)
#define ALPHA_COLOR_LOOKUP_23_3_RED_MASK              (0x1f << 27)
#define ALPHA_COLOR_LOOKUP_23_3_GREEN_MASK            (0x3f << 21)
#define ALPHA_COLOR_LOOKUP_23_3_BLUE_MASK             (0x1f << 16)
#define ALPHA_COLOR_LOOKUP_23_2_MASK                  0xffff
#define ALPHA_COLOR_LOOKUP_23_2_RED_MASK              (0x1f << 11)
#define ALPHA_COLOR_LOOKUP_23_2_GREEN_MASK            (0x3f << 5)
#define ALPHA_COLOR_LOOKUP_23_2_BLUE_MASK             0x1f

#define ALPHA_COLOR_LOOKUP_45                         0x080120
#define ALPHA_COLOR_LOOKUP_45_5_MASK                  (0xffff << 16)
#define ALPHA_COLOR_LOOKUP_45_5_RED_MASK              (0x1f << 27)
#define ALPHA_COLOR_LOOKUP_45_5_GREEN_MASK            (0x3f << 21)
#define ALPHA_COLOR_LOOKUP_45_5_BLUE_MASK             (0x1f << 16)
#define ALPHA_COLOR_LOOKUP_45_4_MASK                  0xffff
#define ALPHA_COLOR_LOOKUP_45_4_RED_MASK              (0x1f << 11)
#define ALPHA_COLOR_LOOKUP_45_4_GREEN_MASK            (0x3f << 5)
#define ALPHA_COLOR_LOOKUP_45_4_BLUE_MASK             0x1f

#define ALPHA_COLOR_LOOKUP_67                         0x080124
#define ALPHA_COLOR_LOOKUP_67_7_MASK                  (0xffff << 16)
#define ALPHA_COLOR_LOOKUP_67_7_RED_MASK              (0x1f << 27)
#define ALPHA_COLOR_LOOKUP_67_7_GREEN_MASK            (0x3f << 21)
#define ALPHA_COLOR_LOOKUP_67_7_BLUE_MASK             (0x1f << 16)
#define ALPHA_COLOR_LOOKUP_67_6_MASK                  0xffff
#define ALPHA_COLOR_LOOKUP_67_6_RED_MASK              (0x1f << 11)
#define ALPHA_COLOR_LOOKUP_67_6_GREEN_MASK            (0x3f << 5)
#define ALPHA_COLOR_LOOKUP_67_6_BLUE_MASK             0x1f

#define ALPHA_COLOR_LOOKUP_89                         0x080128
#define ALPHA_COLOR_LOOKUP_89_9_MASK                  (0xffff << 16)
#define ALPHA_COLOR_LOOKUP_89_9_RED_MASK              (0x1f << 27)
#define ALPHA_COLOR_LOOKUP_89_9_GREEN_MASK            (0x3f << 21)
#define ALPHA_COLOR_LOOKUP_89_9_BLUE_MASK             (0x1f << 16)
#define ALPHA_COLOR_LOOKUP_89_8_MASK                  0xffff
#define ALPHA_COLOR_LOOKUP_89_8_RED_MASK              (0x1f << 11)
#define ALPHA_COLOR_LOOKUP_89_8_GREEN_MASK            (0x3f << 5)
#define ALPHA_COLOR_LOOKUP_89_8_BLUE_MASK             0x1f

#define ALPHA_COLOR_LOOKUP_AB                         0x08012C
#define ALPHA_COLOR_LOOKUP_AB_B_MASK                  (0xffff << 16)
#define ALPHA_COLOR_LOOKUP_AB_B_RED_MASK              (0x1f << 27)
#define ALPHA_COLOR_LOOKUP_AB_B_GREEN_MASK            (0x3f << 21)
#define ALPHA_COLOR_LOOKUP_AB_B_BLUE_MASK             (0x1f << 16)
#define ALPHA_COLOR_LOOKUP_AB_A_MASK                  0xffff
#define ALPHA_COLOR_LOOKUP_AB_A_RED_MASK              (0x1f << 11)
#define ALPHA_COLOR_LOOKUP_AB_A_GREEN_MASK            (0x3f << 5)
#define ALPHA_COLOR_LOOKUP_AB_A_BLUE_MASK             0x1f

#define ALPHA_COLOR_LOOKUP_CD                         0x080130
#define ALPHA_COLOR_LOOKUP_CD_D_MASK                  (0xffff << 16)
#define ALPHA_COLOR_LOOKUP_CD_D_RED_MASK              (0x1f << 27)
#define ALPHA_COLOR_LOOKUP_CD_D_GREEN_MASK            (0x3f << 21)
#define ALPHA_COLOR_LOOKUP_CD_D_BLUE_MASK             (0x1f << 16)
#define ALPHA_COLOR_LOOKUP_CD_C_MASK                  0xffff
#define ALPHA_COLOR_LOOKUP_CD_C_RED_MASK              (0x1f << 11)
#define ALPHA_COLOR_LOOKUP_CD_C_GREEN_MASK            (0x3f << 5)
#define ALPHA_COLOR_LOOKUP_CD_C_BLUE_MASK             0x1f

#define ALPHA_COLOR_LOOKUP_EF                         0x080134
#define ALPHA_COLOR_LOOKUP_EF_F_MASK                  (0xffff << 16)
#define ALPHA_COLOR_LOOKUP_EF_F_RED_MASK              (0x1f << 27)
#define ALPHA_COLOR_LOOKUP_EF_F_GREEN_MASK            (0x3f << 21)
#define ALPHA_COLOR_LOOKUP_EF_F_BLUE_MASK             (0x1f << 16)
#define ALPHA_COLOR_LOOKUP_EF_E_MASK                  0xffff
#define ALPHA_COLOR_LOOKUP_EF_E_RED_MASK              (0x1f << 11)
#define ALPHA_COLOR_LOOKUP_EF_E_GREEN_MASK            (0x3f << 5)
#define ALPHA_COLOR_LOOKUP_EF_E_BLUE_MASK             0x1f

/* CRT Graphics Control */

#define CRT_DISPLAY_CTRL                              0x080200
#define CRT_DISPLAY_CTRL_RESERVED_MASK                0xfb008200

/* SM750LE definition */
#define CRT_DISPLAY_CTRL_DPMS_SHIFT                   30
#define CRT_DISPLAY_CTRL_DPMS_MASK                    (0x3 << 30)
#define CRT_DISPLAY_CTRL_DPMS_0                       (0x0 << 30)
#define CRT_DISPLAY_CTRL_DPMS_1                       (0x1 << 30)
#define CRT_DISPLAY_CTRL_DPMS_2                       (0x2 << 30)
#define CRT_DISPLAY_CTRL_DPMS_3                       (0x3 << 30)
#define CRT_DISPLAY_CTRL_CLK_MASK                     (0x7 << 27)
#define CRT_DISPLAY_CTRL_CLK_PLL25                    (0x0 << 27)
#define CRT_DISPLAY_CTRL_CLK_PLL41                    (0x1 << 27)
#define CRT_DISPLAY_CTRL_CLK_PLL62                    (0x2 << 27)
#define CRT_DISPLAY_CTRL_CLK_PLL65                    (0x3 << 27)
#define CRT_DISPLAY_CTRL_CLK_PLL74                    (0x4 << 27)
#define CRT_DISPLAY_CTRL_CLK_PLL80                    (0x5 << 27)
#define CRT_DISPLAY_CTRL_CLK_PLL108                   (0x6 << 27)
#define CRT_DISPLAY_CTRL_CLK_RESERVED                 (0x7 << 27)
#define CRT_DISPLAY_CTRL_SHIFT_VGA_DAC                BIT(26)

/* SM750LE definition */
#define CRT_DISPLAY_CTRL_CRTSELECT                    BIT(25)
#define CRT_DISPLAY_CTRL_RGBBIT                       BIT(24)

#ifndef VALIDATION_CHIP
    #define CRT_DISPLAY_CTRL_CENTERING                BIT(24)
#endif
#define CRT_DISPLAY_CTRL_LOCK_TIMING                  BIT(23)
#define CRT_DISPLAY_CTRL_EXPANSION                    BIT(22)
#define CRT_DISPLAY_CTRL_VERTICAL_MODE                BIT(21)
#define CRT_DISPLAY_CTRL_HORIZONTAL_MODE              BIT(20)
#define CRT_DISPLAY_CTRL_SELECT_SHIFT                 18
#define CRT_DISPLAY_CTRL_SELECT_MASK                  (0x3 << 18)
#define CRT_DISPLAY_CTRL_SELECT_PANEL                 (0x0 << 18)
#define CRT_DISPLAY_CTRL_SELECT_VGA                   (0x1 << 18)
#define CRT_DISPLAY_CTRL_SELECT_CRT                   (0x2 << 18)
#define CRT_DISPLAY_CTRL_FIFO_MASK                    (0x3 << 16)
#define CRT_DISPLAY_CTRL_FIFO_1                       (0x0 << 16)
#define CRT_DISPLAY_CTRL_FIFO_3                       (0x1 << 16)
#define CRT_DISPLAY_CTRL_FIFO_7                       (0x2 << 16)
#define CRT_DISPLAY_CTRL_FIFO_11                      (0x3 << 16)
#define CRT_DISPLAY_CTRL_BLANK                        BIT(10)
#define CRT_DISPLAY_CTRL_PIXEL_MASK                   (0xf << 4)
#define CRT_DISPLAY_CTRL_FORMAT_MASK                  (0x3 << 0)
#define CRT_DISPLAY_CTRL_FORMAT_8                     (0x0 << 0)
#define CRT_DISPLAY_CTRL_FORMAT_16                    (0x1 << 0)
#define CRT_DISPLAY_CTRL_FORMAT_32                    (0x2 << 0)

#define CRT_FB_ADDRESS                                0x080204
#define CRT_FB_ADDRESS_STATUS                         BIT(31)
#define CRT_FB_ADDRESS_EXT                            BIT(27)
#define CRT_FB_ADDRESS_ADDRESS_MASK                   0x3ffffff

#define CRT_FB_WIDTH                                  0x080208
#define CRT_FB_WIDTH_WIDTH_SHIFT                      16
#define CRT_FB_WIDTH_WIDTH_MASK                       (0x3fff << 16)
#define CRT_FB_WIDTH_OFFSET_MASK                      0x3fff

#define CRT_HORIZONTAL_TOTAL                          0x08020C
#define CRT_HORIZONTAL_TOTAL_TOTAL_SHIFT              16
#define CRT_HORIZONTAL_TOTAL_TOTAL_MASK               (0xfff << 16)
#define CRT_HORIZONTAL_TOTAL_DISPLAY_END_MASK         0xfff

#define CRT_HORIZONTAL_SYNC                           0x080210
#define CRT_HORIZONTAL_SYNC_WIDTH_SHIFT               16
#define CRT_HORIZONTAL_SYNC_WIDTH_MASK                (0xff << 16)
#define CRT_HORIZONTAL_SYNC_START_MASK                0xfff

#define CRT_VERTICAL_TOTAL                            0x080214
#define CRT_VERTICAL_TOTAL_TOTAL_SHIFT                16
#define CRT_VERTICAL_TOTAL_TOTAL_MASK                 (0x7ff << 16)
#define CRT_VERTICAL_TOTAL_DISPLAY_END_MASK           (0x7ff)

#define CRT_VERTICAL_SYNC                             0x080218
#define CRT_VERTICAL_SYNC_HEIGHT_SHIFT                16
#define CRT_VERTICAL_SYNC_HEIGHT_MASK                 (0x3f << 16)
#define CRT_VERTICAL_SYNC_START_MASK                  0x7ff

#define CRT_SIGNATURE_ANALYZER                        0x08021C
#define CRT_SIGNATURE_ANALYZER_STATUS_MASK            (0xffff << 16)
#define CRT_SIGNATURE_ANALYZER_ENABLE                 BIT(3)
#define CRT_SIGNATURE_ANALYZER_RESET                  BIT(2)
#define CRT_SIGNATURE_ANALYZER_SOURCE_MASK            0x3
#define CRT_SIGNATURE_ANALYZER_SOURCE_RED             0
#define CRT_SIGNATURE_ANALYZER_SOURCE_GREEN           1
#define CRT_SIGNATURE_ANALYZER_SOURCE_BLUE            2

#define CRT_CURRENT_LINE                              0x080220
#define CRT_CURRENT_LINE_LINE_MASK                    0x7ff

#define CRT_MONITOR_DETECT                            0x080224
#define CRT_MONITOR_DETECT_VALUE                      BIT(25)
#define CRT_MONITOR_DETECT_ENABLE                     BIT(24)
#define CRT_MONITOR_DETECT_RED_MASK                   (0xff << 16)
#define CRT_MONITOR_DETECT_GREEN_MASK                 (0xff << 8)
#define CRT_MONITOR_DETECT_BLUE_MASK                  0xff

#define CRT_SCALE                                     0x080228
#define CRT_SCALE_VERTICAL_MODE                       BIT(31)
#define CRT_SCALE_VERTICAL_SCALE_MASK                 (0xfff << 16)
#define CRT_SCALE_HORIZONTAL_MODE                     BIT(15)
#define CRT_SCALE_HORIZONTAL_SCALE_MASK               0xfff

/* CRT Cursor Control */

#define CRT_HWC_ADDRESS                               0x080230
#define CRT_HWC_ADDRESS_ENABLE                        BIT(31)
#define CRT_HWC_ADDRESS_EXT                           BIT(27)
#define CRT_HWC_ADDRESS_ADDRESS_MASK                  0x3ffffff

#define CRT_HWC_LOCATION                              0x080234
#define CRT_HWC_LOCATION_TOP                          BIT(27)
#define CRT_HWC_LOCATION_Y_MASK                       (0x7ff << 16)
#define CRT_HWC_LOCATION_LEFT                         BIT(11)
#define CRT_HWC_LOCATION_X_MASK                       0x7ff

#define CRT_HWC_COLOR_12                              0x080238
#define CRT_HWC_COLOR_12_2_RGB565_MASK                (0xffff << 16)
#define CRT_HWC_COLOR_12_1_RGB565_MASK                0xffff

#define CRT_HWC_COLOR_3                               0x08023C
#define CRT_HWC_COLOR_3_RGB565_MASK                   0xffff

/* This vertical expansion below start at 0x080240 ~ 0x080264 */
#define CRT_VERTICAL_EXPANSION                        0x080240
#ifndef VALIDATION_CHIP
    #define CRT_VERTICAL_CENTERING_VALUE_MASK         (0xff << 24)
#endif
#define CRT_VERTICAL_EXPANSION_COMPARE_VALUE_MASK     (0xff << 16)
#define CRT_VERTICAL_EXPANSION_LINE_BUFFER_MASK       (0xf << 12)
#define CRT_VERTICAL_EXPANSION_SCALE_FACTOR_MASK      0xfff

/* This horizontal expansion below start at 0x080268 ~ 0x08027C */
#define CRT_HORIZONTAL_EXPANSION                      0x080268
#ifndef VALIDATION_CHIP
    #define CRT_HORIZONTAL_CENTERING_VALUE_MASK       (0xff << 24)
#endif
#define CRT_HORIZONTAL_EXPANSION_COMPARE_VALUE_MASK   (0xff << 16)
#define CRT_HORIZONTAL_EXPANSION_SCALE_FACTOR_MASK    0xfff

#ifndef VALIDATION_CHIP
    /* Auto Centering */
    #define CRT_AUTO_CENTERING_TL                     0x080280
    #define CRT_AUTO_CENTERING_TL_TOP_MASK            (0x7ff << 16)
    #define CRT_AUTO_CENTERING_TL_LEFT_MASK           0x7ff

    #define CRT_AUTO_CENTERING_BR                     0x080284
    #define CRT_AUTO_CENTERING_BR_BOTTOM_MASK         (0x7ff << 16)
    #define CRT_AUTO_CENTERING_BR_BOTTOM_SHIFT        16
    #define CRT_AUTO_CENTERING_BR_RIGHT_MASK          0x7ff
#endif

/* sm750le new register to control panel output */
#define DISPLAY_CONTROL_750LE			      0x80288
/* Palette RAM */

/* Panel Palette register starts at 0x080400 ~ 0x0807FC */
#define PANEL_PALETTE_RAM                             0x080400

/* Panel Palette register starts at 0x080C00 ~ 0x080FFC */
#define CRT_PALETTE_RAM                               0x080C00

/* Color Space Conversion registers. */

#define CSC_Y_SOURCE_BASE                               0x1000C8
#define CSC_Y_SOURCE_BASE_EXT                           BIT(27)
#define CSC_Y_SOURCE_BASE_CS                            BIT(26)
#define CSC_Y_SOURCE_BASE_ADDRESS_MASK                  0x3ffffff

#define CSC_CONSTANTS                                   0x1000CC
#define CSC_CONSTANTS_Y_MASK                            (0xff << 24)
#define CSC_CONSTANTS_R_MASK                            (0xff << 16)
#define CSC_CONSTANTS_G_MASK                            (0xff << 8)
#define CSC_CONSTANTS_B_MASK                            0xff

#define CSC_Y_SOURCE_X                                  0x1000D0
#define CSC_Y_SOURCE_X_INTEGER_MASK                     (0x7ff << 16)
#define CSC_Y_SOURCE_X_FRACTION_MASK                    (0x1fff << 3)

#define CSC_Y_SOURCE_Y                                  0x1000D4
#define CSC_Y_SOURCE_Y_INTEGER_MASK                     (0xfff << 16)
#define CSC_Y_SOURCE_Y_FRACTION_MASK                    (0x1fff << 3)

#define CSC_U_SOURCE_BASE                               0x1000D8
#define CSC_U_SOURCE_BASE_EXT                           BIT(27)
#define CSC_U_SOURCE_BASE_CS                            BIT(26)
#define CSC_U_SOURCE_BASE_ADDRESS_MASK                  0x3ffffff

#define CSC_V_SOURCE_BASE                               0x1000DC
#define CSC_V_SOURCE_BASE_EXT                           BIT(27)
#define CSC_V_SOURCE_BASE_CS                            BIT(26)
#define CSC_V_SOURCE_BASE_ADDRESS_MASK                  0x3ffffff

#define CSC_SOURCE_DIMENSION                            0x1000E0
#define CSC_SOURCE_DIMENSION_X_MASK                     (0xffff << 16)
#define CSC_SOURCE_DIMENSION_Y_MASK                     0xffff

#define CSC_SOURCE_PITCH                                0x1000E4
#define CSC_SOURCE_PITCH_Y_MASK                         (0xffff << 16)
#define CSC_SOURCE_PITCH_UV_MASK                        0xffff

#define CSC_DESTINATION                                 0x1000E8
#define CSC_DESTINATION_WRAP                            BIT(31)
#define CSC_DESTINATION_X_MASK                          (0xfff << 16)
#define CSC_DESTINATION_Y_MASK                          0xfff

#define CSC_DESTINATION_DIMENSION                       0x1000EC
#define CSC_DESTINATION_DIMENSION_X_MASK                (0xffff << 16)
#define CSC_DESTINATION_DIMENSION_Y_MASK                0xffff

#define CSC_DESTINATION_PITCH                           0x1000F0
#define CSC_DESTINATION_PITCH_X_MASK                    (0xffff << 16)
#define CSC_DESTINATION_PITCH_Y_MASK                    0xffff

#define CSC_SCALE_FACTOR                                0x1000F4
#define CSC_SCALE_FACTOR_HORIZONTAL_MASK                (0xffff << 16)
#define CSC_SCALE_FACTOR_VERTICAL_MASK                  0xffff

#define CSC_DESTINATION_BASE                            0x1000F8
#define CSC_DESTINATION_BASE_EXT                        BIT(27)
#define CSC_DESTINATION_BASE_CS                         BIT(26)
#define CSC_DESTINATION_BASE_ADDRESS_MASK               0x3ffffff

#define CSC_CONTROL                                     0x1000FC
#define CSC_CONTROL_STATUS                              BIT(31)
#define CSC_CONTROL_SOURCE_FORMAT_MASK                  (0x7 << 28)
#define CSC_CONTROL_SOURCE_FORMAT_YUV422                (0x0 << 28)
#define CSC_CONTROL_SOURCE_FORMAT_YUV420I               (0x1 << 28)
#define CSC_CONTROL_SOURCE_FORMAT_YUV420                (0x2 << 28)
#define CSC_CONTROL_SOURCE_FORMAT_YVU9                  (0x3 << 28)
#define CSC_CONTROL_SOURCE_FORMAT_IYU1                  (0x4 << 28)
#define CSC_CONTROL_SOURCE_FORMAT_IYU2                  (0x5 << 28)
#define CSC_CONTROL_SOURCE_FORMAT_RGB565                (0x6 << 28)
#define CSC_CONTROL_SOURCE_FORMAT_RGB8888               (0x7 << 28)
#define CSC_CONTROL_DESTINATION_FORMAT_MASK             (0x3 << 26)
#define CSC_CONTROL_DESTINATION_FORMAT_RGB565           (0x0 << 26)
#define CSC_CONTROL_DESTINATION_FORMAT_RGB8888          (0x1 << 26)
#define CSC_CONTROL_HORIZONTAL_FILTER                   BIT(25)
#define CSC_CONTROL_VERTICAL_FILTER                     BIT(24)
#define CSC_CONTROL_BYTE_ORDER                          BIT(23)

#define DE_DATA_PORT                                    0x110000

#define I2C_BYTE_COUNT                                  0x010040
#define I2C_BYTE_COUNT_COUNT_MASK                       0xf

#define I2C_CTRL                                        0x010041
#define I2C_CTRL_INT                                    BIT(4)
#define I2C_CTRL_DIR                                    BIT(3)
#define I2C_CTRL_CTRL                                   BIT(2)
#define I2C_CTRL_MODE                                   BIT(1)
#define I2C_CTRL_EN                                     BIT(0)

#define I2C_STATUS                                      0x010042
#define I2C_STATUS_TX                                   BIT(3)
#define I2C_STATUS_ERR                                  BIT(2)
#define I2C_STATUS_ACK                                  BIT(1)
#define I2C_STATUS_BSY                                  BIT(0)

#define I2C_RESET                                       0x010042
#define I2C_RESET_BUS_ERROR                             BIT(2)

#define I2C_SLAVE_ADDRESS                               0x010043
#define I2C_SLAVE_ADDRESS_ADDRESS_MASK                  (0x7f << 1)
#define I2C_SLAVE_ADDRESS_RW                            BIT(0)

#define I2C_DATA0                                       0x010044
#define I2C_DATA1                                       0x010045
#define I2C_DATA2                                       0x010046
#define I2C_DATA3                                       0x010047
#define I2C_DATA4                                       0x010048
#define I2C_DATA5                                       0x010049
#define I2C_DATA6                                       0x01004A
#define I2C_DATA7                                       0x01004B
#define I2C_DATA8                                       0x01004C
#define I2C_DATA9                                       0x01004D
#define I2C_DATA10                                      0x01004E
#define I2C_DATA11                                      0x01004F
#define I2C_DATA12                                      0x010050
#define I2C_DATA13                                      0x010051
#define I2C_DATA14                                      0x010052
#define I2C_DATA15                                      0x010053


#define ZV0_CAPTURE_CTRL                                0x090000
#define ZV0_CAPTURE_CTRL_FIELD_INPUT                    BIT(27)
#define ZV0_CAPTURE_CTRL_SCAN                           BIT(26)
#define ZV0_CAPTURE_CTRL_CURRENT_BUFFER                 BIT(25)
#define ZV0_CAPTURE_CTRL_VERTICAL_SYNC                  BIT(24)
#define ZV0_CAPTURE_CTRL_ADJ                            BIT(19)
#define ZV0_CAPTURE_CTRL_HA                             BIT(18)
#define ZV0_CAPTURE_CTRL_VSK                            BIT(17)
#define ZV0_CAPTURE_CTRL_HSK                            BIT(16)
#define ZV0_CAPTURE_CTRL_FD                             BIT(15)
#define ZV0_CAPTURE_CTRL_VP                             BIT(14)
#define ZV0_CAPTURE_CTRL_HP                             BIT(13)
#define ZV0_CAPTURE_CTRL_CP                             BIT(12)
#define ZV0_CAPTURE_CTRL_UVS                            BIT(11)
#define ZV0_CAPTURE_CTRL_BS                             BIT(10)
#define ZV0_CAPTURE_CTRL_CS                             BIT(9)
#define ZV0_CAPTURE_CTRL_CF                             BIT(8)
#define ZV0_CAPTURE_CTRL_FS                             BIT(7)
#define ZV0_CAPTURE_CTRL_WEAVE                          BIT(6)
#define ZV0_CAPTURE_CTRL_BOB                            BIT(5)
#define ZV0_CAPTURE_CTRL_DB                             BIT(4)
#define ZV0_CAPTURE_CTRL_CC                             BIT(3)
#define ZV0_CAPTURE_CTRL_RGB                            BIT(2)
#define ZV0_CAPTURE_CTRL_656                            BIT(1)
#define ZV0_CAPTURE_CTRL_CAP                            BIT(0)

#define ZV0_CAPTURE_CLIP                                0x090004
#define ZV0_CAPTURE_CLIP_EYCLIP_MASK                    (0x3ff << 16)
#define ZV0_CAPTURE_CLIP_XCLIP_MASK                     0x3ff

#define ZV0_CAPTURE_SIZE                                0x090008
#define ZV0_CAPTURE_SIZE_HEIGHT_MASK                    (0x7ff << 16)
#define ZV0_CAPTURE_SIZE_WIDTH_MASK                     0x7ff

#define ZV0_CAPTURE_BUF0_ADDRESS                        0x09000C
#define ZV0_CAPTURE_BUF0_ADDRESS_STATUS                 BIT(31)
#define ZV0_CAPTURE_BUF0_ADDRESS_EXT                    BIT(27)
#define ZV0_CAPTURE_BUF0_ADDRESS_CS                     BIT(26)
#define ZV0_CAPTURE_BUF0_ADDRESS_ADDRESS_MASK           0x3ffffff

#define ZV0_CAPTURE_BUF1_ADDRESS                        0x090010
#define ZV0_CAPTURE_BUF1_ADDRESS_STATUS                 BIT(31)
#define ZV0_CAPTURE_BUF1_ADDRESS_EXT                    BIT(27)
#define ZV0_CAPTURE_BUF1_ADDRESS_CS                     BIT(26)
#define ZV0_CAPTURE_BUF1_ADDRESS_ADDRESS_MASK           0x3ffffff

#define ZV0_CAPTURE_BUF_OFFSET                          0x090014
#ifndef VALIDATION_CHIP
    #define ZV0_CAPTURE_BUF_OFFSET_YCLIP_ODD_FIELD      (0x3ff << 16)
#endif
#define ZV0_CAPTURE_BUF_OFFSET_OFFSET_MASK              0xffff

#define ZV0_CAPTURE_FIFO_CTRL                           0x090018
#define ZV0_CAPTURE_FIFO_CTRL_FIFO_MASK                 0x7
#define ZV0_CAPTURE_FIFO_CTRL_FIFO_0                    0
#define ZV0_CAPTURE_FIFO_CTRL_FIFO_1                    1
#define ZV0_CAPTURE_FIFO_CTRL_FIFO_2                    2
#define ZV0_CAPTURE_FIFO_CTRL_FIFO_3                    3
#define ZV0_CAPTURE_FIFO_CTRL_FIFO_4                    4
#define ZV0_CAPTURE_FIFO_CTRL_FIFO_5                    5
#define ZV0_CAPTURE_FIFO_CTRL_FIFO_6                    6
#define ZV0_CAPTURE_FIFO_CTRL_FIFO_7                    7

#define ZV0_CAPTURE_YRGB_CONST                          0x09001C
#define ZV0_CAPTURE_YRGB_CONST_Y_MASK                   (0xff << 24)
#define ZV0_CAPTURE_YRGB_CONST_R_MASK                   (0xff << 16)
#define ZV0_CAPTURE_YRGB_CONST_G_MASK                   (0xff << 8)
#define ZV0_CAPTURE_YRGB_CONST_B_MASK                   0xff

#define ZV0_CAPTURE_LINE_COMP                           0x090020
#define ZV0_CAPTURE_LINE_COMP_LC_MASK                   0x7ff

/* ZV1 */

#define ZV1_CAPTURE_CTRL                                0x098000
#define ZV1_CAPTURE_CTRL_FIELD_INPUT                    BIT(27)
#define ZV1_CAPTURE_CTRL_SCAN                           BIT(26)
#define ZV1_CAPTURE_CTRL_CURRENT_BUFFER                 BIT(25)
#define ZV1_CAPTURE_CTRL_VERTICAL_SYNC                  BIT(24)
#define ZV1_CAPTURE_CTRL_PANEL                          BIT(20)
#define ZV1_CAPTURE_CTRL_ADJ                            BIT(19)
#define ZV1_CAPTURE_CTRL_HA                             BIT(18)
#define ZV1_CAPTURE_CTRL_VSK                            BIT(17)
#define ZV1_CAPTURE_CTRL_HSK                            BIT(16)
#define ZV1_CAPTURE_CTRL_FD                             BIT(15)
#define ZV1_CAPTURE_CTRL_VP                             BIT(14)
#define ZV1_CAPTURE_CTRL_HP                             BIT(13)
#define ZV1_CAPTURE_CTRL_CP                             BIT(12)
#define ZV1_CAPTURE_CTRL_UVS                            BIT(11)
#define ZV1_CAPTURE_CTRL_BS                             BIT(10)
#define ZV1_CAPTURE_CTRL_CS                             BIT(9)
#define ZV1_CAPTURE_CTRL_CF                             BIT(8)
#define ZV1_CAPTURE_CTRL_FS                             BIT(7)
#define ZV1_CAPTURE_CTRL_WEAVE                          BIT(6)
#define ZV1_CAPTURE_CTRL_BOB                            BIT(5)
#define ZV1_CAPTURE_CTRL_DB                             BIT(4)
#define ZV1_CAPTURE_CTRL_CC                             BIT(3)
#define ZV1_CAPTURE_CTRL_RGB                            BIT(2)
#define ZV1_CAPTURE_CTRL_656                            BIT(1)
#define ZV1_CAPTURE_CTRL_CAP                            BIT(0)

#define ZV1_CAPTURE_CLIP                                0x098004
#define ZV1_CAPTURE_CLIP_YCLIP_MASK                     (0x3ff << 16)
#define ZV1_CAPTURE_CLIP_XCLIP_MASK                     0x3ff

#define ZV1_CAPTURE_SIZE                                0x098008
#define ZV1_CAPTURE_SIZE_HEIGHT_MASK                    (0x7ff << 16)
#define ZV1_CAPTURE_SIZE_WIDTH_MASK                     0x7ff

#define ZV1_CAPTURE_BUF0_ADDRESS                        0x09800C
#define ZV1_CAPTURE_BUF0_ADDRESS_STATUS                 BIT(31)
#define ZV1_CAPTURE_BUF0_ADDRESS_EXT                    BIT(27)
#define ZV1_CAPTURE_BUF0_ADDRESS_CS                     BIT(26)
#define ZV1_CAPTURE_BUF0_ADDRESS_ADDRESS_MASK           0x3ffffff

#define ZV1_CAPTURE_BUF1_ADDRESS                        0x098010
#define ZV1_CAPTURE_BUF1_ADDRESS_STATUS                 BIT(31)
#define ZV1_CAPTURE_BUF1_ADDRESS_EXT                    BIT(27)
#define ZV1_CAPTURE_BUF1_ADDRESS_CS                     BIT(26)
#define ZV1_CAPTURE_BUF1_ADDRESS_ADDRESS_MASK           0x3ffffff

#define ZV1_CAPTURE_BUF_OFFSET                          0x098014
#define ZV1_CAPTURE_BUF_OFFSET_OFFSET_MASK              0xffff

#define ZV1_CAPTURE_FIFO_CTRL                           0x098018
#define ZV1_CAPTURE_FIFO_CTRL_FIFO_MASK                 0x7
#define ZV1_CAPTURE_FIFO_CTRL_FIFO_0                    0
#define ZV1_CAPTURE_FIFO_CTRL_FIFO_1                    1
#define ZV1_CAPTURE_FIFO_CTRL_FIFO_2                    2
#define ZV1_CAPTURE_FIFO_CTRL_FIFO_3                    3
#define ZV1_CAPTURE_FIFO_CTRL_FIFO_4                    4
#define ZV1_CAPTURE_FIFO_CTRL_FIFO_5                    5
#define ZV1_CAPTURE_FIFO_CTRL_FIFO_6                    6
#define ZV1_CAPTURE_FIFO_CTRL_FIFO_7                    7

#define ZV1_CAPTURE_YRGB_CONST                          0x09801C
#define ZV1_CAPTURE_YRGB_CONST_Y_MASK                   (0xff << 24)
#define ZV1_CAPTURE_YRGB_CONST_R_MASK                   (0xff << 16)
#define ZV1_CAPTURE_YRGB_CONST_G_MASK                   (0xff << 8)
#define ZV1_CAPTURE_YRGB_CONST_B_MASK                   0xff

#define DMA_1_SOURCE                                    0x0D0010
#define DMA_1_SOURCE_ADDRESS_EXT                        BIT(27)
#define DMA_1_SOURCE_ADDRESS_CS                         BIT(26)
#define DMA_1_SOURCE_ADDRESS_MASK                       0x3ffffff

#define DMA_1_DESTINATION                               0x0D0014
#define DMA_1_DESTINATION_ADDRESS_EXT                   BIT(27)
#define DMA_1_DESTINATION_ADDRESS_CS                    BIT(26)
#define DMA_1_DESTINATION_ADDRESS_MASK                  0x3ffffff

#define DMA_1_SIZE_CONTROL                              0x0D0018
#define DMA_1_SIZE_CONTROL_STATUS                       BIT(31)
#define DMA_1_SIZE_CONTROL_SIZE_MASK                    0xffffff

#define DMA_ABORT_INTERRUPT                             0x0D0020
#define DMA_ABORT_INTERRUPT_ABORT_1                     BIT(5)
#define DMA_ABORT_INTERRUPT_ABORT_0                     BIT(4)
#define DMA_ABORT_INTERRUPT_INT_1                       BIT(1)
#define DMA_ABORT_INTERRUPT_INT_0                       BIT(0)

/* Default i2c CLK and Data GPIO. These are the default i2c pins */
#define DEFAULT_I2C_SCL                     30
#define DEFAULT_I2C_SDA                     31


#define GPIO_DATA_SM750LE                               0x020018
#define GPIO_DATA_SM750LE_1                             BIT(1)
#define GPIO_DATA_SM750LE_0                             BIT(0)

#define GPIO_DATA_DIRECTION_SM750LE                     0x02001C
#define GPIO_DATA_DIRECTION_SM750LE_1                   BIT(1)
#define GPIO_DATA_DIRECTION_SM750LE_0                   BIT(0)


#endif
