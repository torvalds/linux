/*
 * arch/arm/mach-sun4i/clock/ccmu/ccu_dbg.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/cpufreq.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "ccm_i.h"


#define print_clk_inf(x, y)     do{printk(#x"."#y":%d\n", aw_ccu_reg->x.y);}while(0)


void clk_dbg_inf(void)
{
    printk("---------------------------------------------\n");
    printk("    dump clock information                   \n");
    printk("---------------------------------------------\n");

    printk("PLL1 infor:\n");
    print_clk_inf(Pll1Ctl, FactorM      );
    print_clk_inf(Pll1Ctl, SigmaEn      );
    print_clk_inf(Pll1Ctl, SigmaIn      );
    print_clk_inf(Pll1Ctl, FactorK      );
    print_clk_inf(Pll1Ctl, FactorN      );
    print_clk_inf(Pll1Ctl, LockTime     );
    print_clk_inf(Pll1Ctl, PLLDivP      );
    print_clk_inf(Pll1Ctl, PLLBias      );
    print_clk_inf(Pll1Ctl, ExchangeEn   );
    print_clk_inf(Pll1Ctl, VCOBias      );
    print_clk_inf(Pll1Ctl, VCORstIn     );
    print_clk_inf(Pll1Ctl, PLLEn        );

    printk("\nPLL2 infor:\n");
    print_clk_inf(Pll2Ctl, VCOBias      );
    print_clk_inf(Pll2Ctl, FactorN      );
    print_clk_inf(Pll2Ctl, PLLBias      );
    print_clk_inf(Pll2Ctl, SigmaOut     );
    print_clk_inf(Pll2Ctl, PLLEn        );

    printk("\nPLL3 infor:\n");
    print_clk_inf(Pll3Ctl, FactorM      );
    print_clk_inf(Pll3Ctl, PLLBias      );
    print_clk_inf(Pll3Ctl, FracSet      );
    print_clk_inf(Pll3Ctl, ModeSel      );
    print_clk_inf(Pll3Ctl, VCOBias      );
    print_clk_inf(Pll3Ctl, DampFactor   );
    print_clk_inf(Pll3Ctl, PLLEn        );

    printk("\nPLL4 infor:\n");
    print_clk_inf(Pll4Ctl, FactorM      );
    print_clk_inf(Pll4Ctl, FactorK      );
    print_clk_inf(Pll4Ctl, FactorN      );
    print_clk_inf(Pll4Ctl, FactorP      );
    print_clk_inf(Pll4Ctl, VCOGain      );
    print_clk_inf(Pll4Ctl, PLLBias      );
    print_clk_inf(Pll4Ctl, VCOBias      );
    print_clk_inf(Pll4Ctl, PLLBypass    );
    print_clk_inf(Pll4Ctl, PLLEn        );
    if (SUNXI_VER_A10C == sw_get_ic_ver())
        print_clk_inf(Pll4Ctl, PllSwitch    );

    printk("\nPLL5 infor:\n");
    print_clk_inf(Pll5Ctl, FactorM      );
    print_clk_inf(Pll5Ctl, FactorM1     );
    print_clk_inf(Pll5Ctl, FactorK      );
    print_clk_inf(Pll5Ctl, LDO2En       );
    print_clk_inf(Pll5Ctl, FactorN      );
    print_clk_inf(Pll5Ctl, VCOGain      );
    print_clk_inf(Pll5Ctl, FactorP      );
    print_clk_inf(Pll5Ctl, BandWidth    );
    print_clk_inf(Pll5Ctl, VCOGainEn    );
    print_clk_inf(Pll5Ctl, PLLBias      );
    print_clk_inf(Pll5Ctl, VCOBias      );
    print_clk_inf(Pll5Ctl, OutputEn     );
    print_clk_inf(Pll5Ctl, PLLBypass    );
    print_clk_inf(Pll5Ctl, PLLEn        );

    printk("\nPLL6 infor:\n");
    print_clk_inf(Pll6Ctl, FactorM      );
    print_clk_inf(Pll6Ctl, FactorK      );
    print_clk_inf(Pll6Ctl, DampFactor   );
    print_clk_inf(Pll6Ctl, FactorN      );
    print_clk_inf(Pll6Ctl, OutputEn     );
    print_clk_inf(Pll6Ctl, BandWidth    );
    print_clk_inf(Pll6Ctl, PLLBias      );
    print_clk_inf(Pll6Ctl, VCOBias      );
    print_clk_inf(Pll6Ctl, PLLBypass    );
    print_clk_inf(Pll6Ctl, PLLEn        );

    printk("\nPLL7 infor:\n");
    print_clk_inf(Pll7Ctl, FactorM      );
    print_clk_inf(Pll7Ctl, PLLBias      );
    print_clk_inf(Pll7Ctl, FracSet      );
    print_clk_inf(Pll7Ctl, ModeSel      );
    print_clk_inf(Pll7Ctl, VCOBias      );
    print_clk_inf(Pll7Ctl, DampFactor   );
    print_clk_inf(Pll7Ctl, PLLEn        );

    printk("\nHOSC infor:\n");
    print_clk_inf(HoscCtl, OSC24MEn     );
    print_clk_inf(HoscCtl, OSC24MGsm    );
    print_clk_inf(HoscCtl, PLLBiasEn    );
    print_clk_inf(HoscCtl, LDOEn        );
    print_clk_inf(HoscCtl, PLLInPower   );
    print_clk_inf(HoscCtl, LDOOutput    );
    print_clk_inf(HoscCtl, KeyField     );

    printk("\nCPU clk infor:\n");
    print_clk_inf(SysClkDiv, AXIClkDiv  );
    print_clk_inf(SysClkDiv, AHBClkDiv  );
    print_clk_inf(SysClkDiv, APB0ClkDiv );
    print_clk_inf(SysClkDiv, AC328ClkSrc);

    printk("\nAPB1 clk infor:\n");
    print_clk_inf(Apb1ClkDiv, ClkDiv    );
    print_clk_inf(Apb1ClkDiv, PreDiv    );
    print_clk_inf(Apb1ClkDiv, ClkSrc    );

    printk("\nAxiGate clk infor:\n");
    print_clk_inf(AxiGate, SdramGate    );

    printk("\nAhbGate0 clk infor:\n");
    print_clk_inf(AhbGate0, Usb0Gate    );
    print_clk_inf(AhbGate0, Ehci0Gate   );
    print_clk_inf(AhbGate0, Ohci0Gate   );
    print_clk_inf(AhbGate0, Ehci1Gate   );
    print_clk_inf(AhbGate0, Ohci1Gate   );
    print_clk_inf(AhbGate0, SsGate      );
    print_clk_inf(AhbGate0, DmaGate     );
    print_clk_inf(AhbGate0, BistGate    );
    print_clk_inf(AhbGate0, Sdmmc0Gate  );
    print_clk_inf(AhbGate0, Sdmmc1Gate  );
    print_clk_inf(AhbGate0, Sdmmc2Gate  );
    print_clk_inf(AhbGate0, Sdmmc3Gate  );
    print_clk_inf(AhbGate0, MsGate      );
    print_clk_inf(AhbGate0, NandGate    );
    print_clk_inf(AhbGate0, SdramGate   );
    print_clk_inf(AhbGate0, AceGate     );
    print_clk_inf(AhbGate0, EmacGate    );
    print_clk_inf(AhbGate0, TsGate      );
    print_clk_inf(AhbGate0, Spi0Gate    );
    print_clk_inf(AhbGate0, Spi1Gate    );
    print_clk_inf(AhbGate0, Spi2Gate    );
    print_clk_inf(AhbGate0, Spi3Gate    );
    print_clk_inf(AhbGate0, PataGate    );
    print_clk_inf(AhbGate0, SataGate    );
    print_clk_inf(AhbGate0, GpsGate     );

    printk("\nAhbGate1 clk infor:\n");
    print_clk_inf(AhbGate1, VeGate      );
    print_clk_inf(AhbGate1, TvdGate     );
    print_clk_inf(AhbGate1, Tve0Gate    );
    print_clk_inf(AhbGate1, Tve1Gate    );
    print_clk_inf(AhbGate1, Lcd0Gate    );
    print_clk_inf(AhbGate1, Lcd1Gate    );
    print_clk_inf(AhbGate1, Csi0Gate    );
    print_clk_inf(AhbGate1, Csi1Gate    );
    print_clk_inf(AhbGate1, HdmiDGate   );
    print_clk_inf(AhbGate1, DeBe0Gate   );
    print_clk_inf(AhbGate1, DeBe1Gate   );
    print_clk_inf(AhbGate1, DeFe0Gate   );
    print_clk_inf(AhbGate1, DeFe1Gate   );
    print_clk_inf(AhbGate1, MpGate      );
    print_clk_inf(AhbGate1, Gpu3DGate   );

    printk("\nApb0Gate clk infor:\n");
    print_clk_inf(Apb0Gate, AddaGate    );
    print_clk_inf(Apb0Gate, SpdifGate   );
    print_clk_inf(Apb0Gate, Ac97Gate    );
    print_clk_inf(Apb0Gate, IisGate     );
    print_clk_inf(Apb0Gate, PioGate     );
    print_clk_inf(Apb0Gate, Ir0Gate     );
    print_clk_inf(Apb0Gate, Ir1Gate     );
    print_clk_inf(Apb0Gate, KeypadGate  );

    printk("\nApb1Gate clk infor:\n");
    print_clk_inf(Apb1Gate, Twi0Gate    );
    print_clk_inf(Apb1Gate, Twi1Gate    );
    print_clk_inf(Apb1Gate, Twi2Gate    );
    print_clk_inf(Apb1Gate, CanGate     );
    print_clk_inf(Apb1Gate, ScrGate     );
    print_clk_inf(Apb1Gate, Ps20Gate    );
    print_clk_inf(Apb1Gate, Ps21Gate    );
    print_clk_inf(Apb1Gate, Uart0Gate   );
    print_clk_inf(Apb1Gate, Uart1Gate   );
    print_clk_inf(Apb1Gate, Uart2Gate   );
    print_clk_inf(Apb1Gate, Uart3Gate   );
    print_clk_inf(Apb1Gate, Uart4Gate   );
    print_clk_inf(Apb1Gate, Uart5Gate   );
    print_clk_inf(Apb1Gate, Uart6Gate   );
    print_clk_inf(Apb1Gate, Uart7Gate   );

    printk("\nNandClk clk infor:\n");
    print_clk_inf(NandClk, ClkDiv       );
    print_clk_inf(NandClk, ClkPreDiv    );
    print_clk_inf(NandClk, ClkSrc       );
    print_clk_inf(NandClk, SpecClkGate  );

    printk("\nMsClk clk infor:\n");
    print_clk_inf(MsClk, ClkDiv         );
    print_clk_inf(MsClk, ClkPreDiv      );
    print_clk_inf(MsClk, ClkSrc         );
    print_clk_inf(MsClk, SpecClkGate    );

    printk("\nSdMmc0Clk clk infor:\n");
    print_clk_inf(SdMmc0Clk, ClkDiv     );
    print_clk_inf(SdMmc0Clk, ClkPreDiv  );
    print_clk_inf(SdMmc0Clk, ClkSrc     );
    print_clk_inf(SdMmc0Clk, SpecClkGate);

    printk("\nSdMmc1Clk clk infor:\n");
    print_clk_inf(SdMmc1Clk, ClkDiv     );
    print_clk_inf(SdMmc1Clk, ClkPreDiv  );
    print_clk_inf(SdMmc1Clk, ClkSrc     );
    print_clk_inf(SdMmc1Clk, SpecClkGate);

    printk("\nSdMmc2Clk clk infor:\n");
    print_clk_inf(SdMmc2Clk, ClkDiv     );
    print_clk_inf(SdMmc2Clk, ClkPreDiv  );
    print_clk_inf(SdMmc2Clk, ClkSrc     );
    print_clk_inf(SdMmc2Clk, SpecClkGate);

    printk("\nSdMmc3Clk clk infor:\n");
    print_clk_inf(SdMmc3Clk, ClkDiv     );
    print_clk_inf(SdMmc3Clk, ClkPreDiv  );
    print_clk_inf(SdMmc3Clk, ClkSrc     );
    print_clk_inf(SdMmc3Clk, SpecClkGate);

    printk("\nTsClk clk infor:\n");
    print_clk_inf(TsClk, ClkDiv         );
    print_clk_inf(TsClk, ClkPreDiv      );
    print_clk_inf(TsClk, ClkSrc         );
    print_clk_inf(TsClk, SpecClkGate    );

    printk("\nSsClk clk infor:\n");
    print_clk_inf(SsClk, ClkDiv         );
    print_clk_inf(SsClk, ClkPreDiv      );
    print_clk_inf(SsClk, ClkSrc         );
    print_clk_inf(SsClk, SpecClkGate    );

    printk("\nSpi0Clk clk infor:\n");
    print_clk_inf(Spi0Clk, ClkDiv       );
    print_clk_inf(Spi0Clk, ClkPreDiv    );
    print_clk_inf(Spi0Clk, ClkSrc       );
    print_clk_inf(Spi0Clk, SpecClkGate  );

    printk("\nSpi1Clk clk infor:\n");
    print_clk_inf(Spi1Clk, ClkDiv       );
    print_clk_inf(Spi1Clk, ClkPreDiv    );
    print_clk_inf(Spi1Clk, ClkSrc       );
    print_clk_inf(Spi1Clk, SpecClkGate  );

    printk("\nSpi2Clk clk infor:\n");
    print_clk_inf(Spi2Clk, ClkDiv       );
    print_clk_inf(Spi2Clk, ClkPreDiv    );
    print_clk_inf(Spi2Clk, ClkSrc       );
    print_clk_inf(Spi2Clk, SpecClkGate  );

    printk("\nPataClk clk infor:\n");
    print_clk_inf(PataClk, ClkDiv       );
    print_clk_inf(PataClk, ClkPreDiv    );
    print_clk_inf(PataClk, ClkSrc       );
    print_clk_inf(PataClk, SpecClkGate  );

    printk("\nIr0Clk clk infor:\n");
    print_clk_inf(Ir0Clk, ClkDiv        );
    print_clk_inf(Ir0Clk, ClkPreDiv     );
    print_clk_inf(Ir0Clk, ClkSrc        );
    print_clk_inf(Ir0Clk, SpecClkGate   );

    printk("\nIr1Clk clk infor:\n");
    print_clk_inf(Ir1Clk, ClkDiv        );
    print_clk_inf(Ir1Clk, ClkPreDiv     );
    print_clk_inf(Ir1Clk, ClkSrc        );
    print_clk_inf(Ir1Clk, SpecClkGate   );

    printk("\nI2sClk clk infor:\n");
    print_clk_inf(I2sClk, ClkDiv        );
    print_clk_inf(I2sClk, SpecClkGate   );


    printk("\nAc97Clk clk infor:\n");
    print_clk_inf(Ac97Clk, ClkDiv       );
    print_clk_inf(Ac97Clk, SpecClkGate  );

    printk("\nSpdifClk clk infor:\n");
    print_clk_inf(SpdifClk, ClkDiv      );
    print_clk_inf(SpdifClk, SpecClkGate );

    printk("\nKeyPadClk clk infor:\n");
    print_clk_inf(KeyPadClk, ClkDiv         );
    print_clk_inf(KeyPadClk, ClkPreDiv      );
    print_clk_inf(KeyPadClk, ClkSrc         );
    print_clk_inf(KeyPadClk, SpecClkGate    );

    printk("\nSataClk clk infor:\n");
    print_clk_inf(SataClk, ClkSrc       );
    print_clk_inf(SataClk, SpecClkGate  );

    printk("\nUsbClk clk infor:\n");
    print_clk_inf(UsbClk, UsbPhy0Rst        );
    print_clk_inf(UsbClk, UsbPhy1Rst        );
    print_clk_inf(UsbClk, UsbPhy2Rst        );
    print_clk_inf(UsbClk, OHCIClkSrc        );
    print_clk_inf(UsbClk, OHCI0SpecClkGate  );
    print_clk_inf(UsbClk, OHCI1SpecClkGate  );
    print_clk_inf(UsbClk, PhySpecClkGate    );

    printk("\nGpsClk clk infor:\n");
    print_clk_inf(GpsClk, Reset         );
    print_clk_inf(GpsClk, SpecClkGate   );

    printk("\nSpi3Clk clk infor:\n");
    print_clk_inf(Spi3Clk, ClkDiv       );
    print_clk_inf(Spi3Clk, ClkPreDiv    );
    print_clk_inf(Spi3Clk, ClkSrc       );
    print_clk_inf(Spi3Clk, SpecClkGate  );

    printk("\nDramGate clk infor:\n");
    print_clk_inf(DramGate, VeGate      );
    print_clk_inf(DramGate, Csi0Gate    );
    print_clk_inf(DramGate, Csi1Gate    );
    print_clk_inf(DramGate, TsGate      );
    print_clk_inf(DramGate, TvdGate     );
    print_clk_inf(DramGate, Tve0Gate    );
    print_clk_inf(DramGate, Tve1Gate    );
    print_clk_inf(DramGate, ClkOutputEn );
    print_clk_inf(DramGate, DeFe0Gate   );
    print_clk_inf(DramGate, DeFe1Gate   );
    print_clk_inf(DramGate, DeBe0Gate   );
    print_clk_inf(DramGate, DeBe1Gate   );
    print_clk_inf(DramGate, DeMpGate    );
    print_clk_inf(DramGate, AceGate     );

    printk("\nDeBe0Clk clk infor:\n");
    print_clk_inf(DeBe0Clk, ClkDiv      );
    print_clk_inf(DeBe0Clk, ClkSrc      );
    print_clk_inf(DeBe0Clk, Reset       );
    print_clk_inf(DeBe0Clk, SpecClkGate );

    printk("\nDeBe1Clk clk infor:\n");
    print_clk_inf(DeBe1Clk, ClkDiv      );
    print_clk_inf(DeBe1Clk, ClkSrc      );
    print_clk_inf(DeBe1Clk, Reset       );
    print_clk_inf(DeBe1Clk, SpecClkGate );

    printk("\nDeFe0Clk clk infor:\n");
    print_clk_inf(DeFe0Clk, ClkDiv      );
    print_clk_inf(DeFe0Clk, ClkSrc      );
    print_clk_inf(DeFe0Clk, Reset       );
    print_clk_inf(DeFe0Clk, SpecClkGate );

    printk("\nDeFe1Clk clk infor:\n");
    print_clk_inf(DeFe1Clk, ClkDiv      );
    print_clk_inf(DeFe1Clk, ClkSrc      );
    print_clk_inf(DeFe1Clk, Reset       );
    print_clk_inf(DeFe1Clk, SpecClkGate );

    printk("\nDeMpClk clk infor:\n");
    print_clk_inf(DeMpClk, ClkDiv       );
    print_clk_inf(DeMpClk, ClkSrc       );
    print_clk_inf(DeMpClk, Reset        );
    print_clk_inf(DeMpClk, SpecClkGate  );

    printk("\nLcd0Ch0Clk clk infor:\n");
    print_clk_inf(Lcd0Ch0Clk, ClkSrc        );
    print_clk_inf(Lcd0Ch0Clk, Reset         );
    print_clk_inf(Lcd0Ch0Clk, SpecClkGate   );

    printk("\nLcd1Ch0Clk clk infor:\n");
    print_clk_inf(Lcd1Ch0Clk, ClkSrc        );
    print_clk_inf(Lcd1Ch0Clk, Reset         );
    print_clk_inf(Lcd1Ch0Clk, SpecClkGate   );

    printk("\nCsiIspClk clk infor:\n");
    print_clk_inf(CsiIspClk, ClkDiv         );
    print_clk_inf(CsiIspClk, ClkSrc         );
    print_clk_inf(CsiIspClk, SpecClkGate    );

    printk("\nTvdClk clk infor:\n");
    print_clk_inf(TvdClk, ClkSrc        );
    print_clk_inf(TvdClk, SpecClkGate   );

    printk("\nLcd0Ch1Clk clk infor:\n");
    print_clk_inf(Lcd0Ch1Clk, ClkDiv        );
    print_clk_inf(Lcd0Ch1Clk, SpecClk1Src   );
    print_clk_inf(Lcd0Ch1Clk, SpecClk1Gate  );
    print_clk_inf(Lcd0Ch1Clk, SpecClk2Src   );
    print_clk_inf(Lcd0Ch1Clk, SpecClk2Gate  );

    printk("\nLcd1Ch1Clk clk infor:\n");
    print_clk_inf(Lcd1Ch1Clk, ClkDiv        );
    print_clk_inf(Lcd1Ch1Clk, SpecClk1Src   );
    print_clk_inf(Lcd1Ch1Clk, SpecClk1Gate  );
    print_clk_inf(Lcd1Ch1Clk, SpecClk2Src   );
    print_clk_inf(Lcd1Ch1Clk, SpecClk2Gate  );

    printk("\nCsi0Clk clk infor:\n");
    print_clk_inf(Csi0Clk, ClkDiv       );
    print_clk_inf(Csi0Clk, ClkSrc       );
    print_clk_inf(Csi0Clk, Reset        );
    print_clk_inf(Csi0Clk, SpecClkGate  );

    printk("\nCsi1Clk clk infor:\n");
    print_clk_inf(Csi1Clk, ClkDiv       );
    print_clk_inf(Csi1Clk, ClkSrc       );
    print_clk_inf(Csi1Clk, Reset        );
    print_clk_inf(Csi1Clk, SpecClkGate  );

    printk("\nVeClk clk infor:\n");
    print_clk_inf(VeClk, Reset          );
    print_clk_inf(VeClk, ClkDiv         );
    print_clk_inf(VeClk, SpecClkGate    );

    printk("\nAddaClk clk infor:\n");
    print_clk_inf(AddaClk, SpecClkGate  );

    printk("\nAvsClk clk infor:\n");
    print_clk_inf(AvsClk, SpecClkGate   );

    printk("\nAceClk clk infor:\n");
    print_clk_inf(AceClk, ClkDiv        );
    print_clk_inf(AceClk, Reset         );
    print_clk_inf(AceClk, ClkSrc        );
    print_clk_inf(AceClk, SpecClkGate   );

    printk("\nLvdsClk clk infor:\n");
    print_clk_inf(LvdsClk, Reset        );

    printk("\nHdmiClk clk infor:\n");
    print_clk_inf(HdmiClk, ClkDiv       );
    print_clk_inf(HdmiClk, ClkSrc       );
    print_clk_inf(HdmiClk, SpecClkGate  );

    printk("\nMaliClk clk infor:\n");
    print_clk_inf(MaliClk, ClkDiv       );
    print_clk_inf(MaliClk, ClkSrc       );
    print_clk_inf(MaliClk, Reset        );
    print_clk_inf(MaliClk, SpecClkGate  );
}
EXPORT_SYMBOL(clk_dbg_inf);

#ifdef CONFIG_PROC_FS

#define sprintf_clk_inf(buf, x, y)     do{seq_printf(buf, "\t"#x"."#y":%d\n", aw_ccu_reg->x.y);}while(0)
static int ccmu_stats_show(struct seq_file *m, void *unused)
{
    seq_printf(m, "---------------------------------------------\n");
    seq_printf(m, "clock information:                           \n");
    seq_printf(m, "---------------------------------------------\n");

    seq_printf(m, "\nPLL1 infor:\n");
    sprintf_clk_inf(m, Pll1Ctl, FactorM      );
    sprintf_clk_inf(m, Pll1Ctl, SigmaEn      );
    sprintf_clk_inf(m, Pll1Ctl, SigmaIn      );
    sprintf_clk_inf(m, Pll1Ctl, FactorK      );
    sprintf_clk_inf(m, Pll1Ctl, FactorN      );
    sprintf_clk_inf(m, Pll1Ctl, LockTime     );
    sprintf_clk_inf(m, Pll1Ctl, PLLDivP      );
    sprintf_clk_inf(m, Pll1Ctl, PLLBias      );
    sprintf_clk_inf(m, Pll1Ctl, ExchangeEn   );
    sprintf_clk_inf(m, Pll1Ctl, VCOBias      );
    sprintf_clk_inf(m, Pll1Ctl, VCORstIn     );
    sprintf_clk_inf(m, Pll1Ctl, PLLEn        );

    seq_printf(m, "\nPLL2 infor(0x%x):\n", *(volatile __u32 *)&aw_ccu_reg->Pll2Ctl);
    sprintf_clk_inf(m, Pll2Ctl, VCOBias      );
    sprintf_clk_inf(m, Pll2Ctl, FactorN      );
    sprintf_clk_inf(m, Pll2Ctl, PLLBias      );
    sprintf_clk_inf(m, Pll2Ctl, SigmaOut     );
    sprintf_clk_inf(m, Pll2Ctl, PLLEn        );

    seq_printf(m, "\nPLL3 infor:\n");
    sprintf_clk_inf(m, Pll3Ctl, FactorM      );
    sprintf_clk_inf(m, Pll3Ctl, PLLBias      );
    sprintf_clk_inf(m, Pll3Ctl, FracSet      );
    sprintf_clk_inf(m, Pll3Ctl, ModeSel      );
    sprintf_clk_inf(m, Pll3Ctl, VCOBias      );
    sprintf_clk_inf(m, Pll3Ctl, DampFactor   );
    sprintf_clk_inf(m, Pll3Ctl, PLLEn        );

    seq_printf(m, "\nPLL4 infor:\n");
    sprintf_clk_inf(m, Pll4Ctl, FactorM      );
    sprintf_clk_inf(m, Pll4Ctl, FactorK      );
    sprintf_clk_inf(m, Pll4Ctl, FactorN      );
    sprintf_clk_inf(m, Pll4Ctl, FactorP      );
    sprintf_clk_inf(m, Pll4Ctl, VCOGain      );
    sprintf_clk_inf(m, Pll4Ctl, PLLBias      );
    sprintf_clk_inf(m, Pll4Ctl, VCOBias      );
    sprintf_clk_inf(m, Pll4Ctl, PLLBypass    );
    sprintf_clk_inf(m, Pll4Ctl, PLLEn        );
    if (SUNXI_VER_A10C == sw_get_ic_ver())
        sprintf_clk_inf(m, Pll4Ctl, PllSwitch   );

    seq_printf(m, "\nPLL5 infor:\n");
    sprintf_clk_inf(m, Pll5Ctl, FactorM      );
    sprintf_clk_inf(m, Pll5Ctl, FactorM1     );
    sprintf_clk_inf(m, Pll5Ctl, FactorK      );
    sprintf_clk_inf(m, Pll5Ctl, LDO2En       );
    sprintf_clk_inf(m, Pll5Ctl, FactorN      );
    sprintf_clk_inf(m, Pll5Ctl, VCOGain      );
    sprintf_clk_inf(m, Pll5Ctl, FactorP      );
    sprintf_clk_inf(m, Pll5Ctl, BandWidth    );
    sprintf_clk_inf(m, Pll5Ctl, VCOGainEn    );
    sprintf_clk_inf(m, Pll5Ctl, PLLBias      );
    sprintf_clk_inf(m, Pll5Ctl, VCOBias      );
    sprintf_clk_inf(m, Pll5Ctl, OutputEn     );
    sprintf_clk_inf(m, Pll5Ctl, PLLBypass    );
    sprintf_clk_inf(m, Pll5Ctl, PLLEn        );

    seq_printf(m, "\nPLL6 infor:\n");
    sprintf_clk_inf(m, Pll6Ctl, FactorM      );
    sprintf_clk_inf(m, Pll6Ctl, FactorK      );
    sprintf_clk_inf(m, Pll6Ctl, DampFactor   );
    sprintf_clk_inf(m, Pll6Ctl, FactorN      );
    sprintf_clk_inf(m, Pll6Ctl, OutputEn     );
    sprintf_clk_inf(m, Pll6Ctl, BandWidth    );
    sprintf_clk_inf(m, Pll6Ctl, PLLBias      );
    sprintf_clk_inf(m, Pll6Ctl, VCOBias      );
    sprintf_clk_inf(m, Pll6Ctl, PLLBypass    );
    sprintf_clk_inf(m, Pll6Ctl, PLLEn        );

    seq_printf(m, "\nPLL7 infor:\n");
    sprintf_clk_inf(m, Pll7Ctl, FactorM      );
    sprintf_clk_inf(m, Pll7Ctl, PLLBias      );
    sprintf_clk_inf(m, Pll7Ctl, FracSet      );
    sprintf_clk_inf(m, Pll7Ctl, ModeSel      );
    sprintf_clk_inf(m, Pll7Ctl, VCOBias      );
    sprintf_clk_inf(m, Pll7Ctl, DampFactor   );
    sprintf_clk_inf(m, Pll7Ctl, PLLEn        );

    seq_printf(m, "\nHOSC infor:\n");
    sprintf_clk_inf(m, HoscCtl, OSC24MEn     );
    sprintf_clk_inf(m, HoscCtl, OSC24MGsm    );
    sprintf_clk_inf(m, HoscCtl, PLLBiasEn    );
    sprintf_clk_inf(m, HoscCtl, LDOEn        );
    sprintf_clk_inf(m, HoscCtl, PLLInPower   );
    sprintf_clk_inf(m, HoscCtl, LDOOutput    );
    sprintf_clk_inf(m, HoscCtl, KeyField     );

    seq_printf(m, "\nCPU clk infor:\n");
    sprintf_clk_inf(m, SysClkDiv, AXIClkDiv  );
    sprintf_clk_inf(m, SysClkDiv, AHBClkDiv  );
    sprintf_clk_inf(m, SysClkDiv, APB0ClkDiv );
    sprintf_clk_inf(m, SysClkDiv, AC328ClkSrc);

    seq_printf(m, "\nAPB1 clk infor:\n");
    sprintf_clk_inf(m, Apb1ClkDiv, ClkDiv    );
    sprintf_clk_inf(m, Apb1ClkDiv, PreDiv    );
    sprintf_clk_inf(m, Apb1ClkDiv, ClkSrc    );

    seq_printf(m, "\nAxiGate clk infor:\n");
    sprintf_clk_inf(m, AxiGate, SdramGate    );

    seq_printf(m, "\nAhbGate0 clk infor:\n");
    sprintf_clk_inf(m, AhbGate0, Usb0Gate    );
    sprintf_clk_inf(m, AhbGate0, Ehci0Gate   );
    sprintf_clk_inf(m, AhbGate0, Ohci0Gate   );
    sprintf_clk_inf(m, AhbGate0, Ehci1Gate   );
    sprintf_clk_inf(m, AhbGate0, Ohci1Gate   );
    sprintf_clk_inf(m, AhbGate0, SsGate      );
    sprintf_clk_inf(m, AhbGate0, DmaGate     );
    sprintf_clk_inf(m, AhbGate0, BistGate    );
    sprintf_clk_inf(m, AhbGate0, Sdmmc0Gate  );
    sprintf_clk_inf(m, AhbGate0, Sdmmc1Gate  );
    sprintf_clk_inf(m, AhbGate0, Sdmmc2Gate  );
    sprintf_clk_inf(m, AhbGate0, Sdmmc3Gate  );
    sprintf_clk_inf(m, AhbGate0, MsGate      );
    sprintf_clk_inf(m, AhbGate0, NandGate    );
    sprintf_clk_inf(m, AhbGate0, SdramGate   );
    sprintf_clk_inf(m, AhbGate0, AceGate     );
    sprintf_clk_inf(m, AhbGate0, EmacGate    );
    sprintf_clk_inf(m, AhbGate0, TsGate      );
    sprintf_clk_inf(m, AhbGate0, Spi0Gate    );
    sprintf_clk_inf(m, AhbGate0, Spi1Gate    );
    sprintf_clk_inf(m, AhbGate0, Spi2Gate    );
    sprintf_clk_inf(m, AhbGate0, Spi3Gate    );
    sprintf_clk_inf(m, AhbGate0, PataGate    );
    sprintf_clk_inf(m, AhbGate0, SataGate    );
    sprintf_clk_inf(m, AhbGate0, GpsGate     );

    seq_printf(m, "\nAhbGate1 clk infor:\n");
    sprintf_clk_inf(m, AhbGate1, VeGate      );
    sprintf_clk_inf(m, AhbGate1, TvdGate     );
    sprintf_clk_inf(m, AhbGate1, Tve0Gate    );
    sprintf_clk_inf(m, AhbGate1, Tve1Gate    );
    sprintf_clk_inf(m, AhbGate1, Lcd0Gate    );
    sprintf_clk_inf(m, AhbGate1, Lcd1Gate    );
    sprintf_clk_inf(m, AhbGate1, Csi0Gate    );
    sprintf_clk_inf(m, AhbGate1, Csi1Gate    );
    sprintf_clk_inf(m, AhbGate1, HdmiDGate   );
    sprintf_clk_inf(m, AhbGate1, DeBe0Gate   );
    sprintf_clk_inf(m, AhbGate1, DeBe1Gate   );
    sprintf_clk_inf(m, AhbGate1, DeFe0Gate   );
    sprintf_clk_inf(m, AhbGate1, DeFe1Gate   );
    sprintf_clk_inf(m, AhbGate1, MpGate      );
    sprintf_clk_inf(m, AhbGate1, Gpu3DGate   );

    seq_printf(m, "\nApb0Gate clk infor:\n");
    sprintf_clk_inf(m, Apb0Gate, AddaGate    );
    sprintf_clk_inf(m, Apb0Gate, SpdifGate   );
    sprintf_clk_inf(m, Apb0Gate, Ac97Gate    );
    sprintf_clk_inf(m, Apb0Gate, IisGate     );
    sprintf_clk_inf(m, Apb0Gate, PioGate     );
    sprintf_clk_inf(m, Apb0Gate, Ir0Gate     );
    sprintf_clk_inf(m, Apb0Gate, Ir1Gate     );
    sprintf_clk_inf(m, Apb0Gate, KeypadGate  );

    seq_printf(m, "\nApb1Gate clk infor:\n");
    sprintf_clk_inf(m, Apb1Gate, Twi0Gate    );
    sprintf_clk_inf(m, Apb1Gate, Twi1Gate    );
    sprintf_clk_inf(m, Apb1Gate, Twi2Gate    );
    sprintf_clk_inf(m, Apb1Gate, CanGate     );
    sprintf_clk_inf(m, Apb1Gate, ScrGate     );
    sprintf_clk_inf(m, Apb1Gate, Ps20Gate    );
    sprintf_clk_inf(m, Apb1Gate, Ps21Gate    );
    sprintf_clk_inf(m, Apb1Gate, Uart0Gate   );
    sprintf_clk_inf(m, Apb1Gate, Uart1Gate   );
    sprintf_clk_inf(m, Apb1Gate, Uart2Gate   );
    sprintf_clk_inf(m, Apb1Gate, Uart3Gate   );
    sprintf_clk_inf(m, Apb1Gate, Uart4Gate   );
    sprintf_clk_inf(m, Apb1Gate, Uart5Gate   );
    sprintf_clk_inf(m, Apb1Gate, Uart6Gate   );
    sprintf_clk_inf(m, Apb1Gate, Uart7Gate   );

    seq_printf(m, "\nNandClk clk infor:\n");
    sprintf_clk_inf(m, NandClk, ClkDiv       );
    sprintf_clk_inf(m, NandClk, ClkPreDiv    );
    sprintf_clk_inf(m, NandClk, ClkSrc       );
    sprintf_clk_inf(m, NandClk, SpecClkGate  );

    seq_printf(m, "\nMsClk clk infor:\n");
    sprintf_clk_inf(m, MsClk, ClkDiv         );
    sprintf_clk_inf(m, MsClk, ClkPreDiv      );
    sprintf_clk_inf(m, MsClk, ClkSrc         );
    sprintf_clk_inf(m, MsClk, SpecClkGate    );

    seq_printf(m, "\nSdMmc0Clk clk infor:\n");
    sprintf_clk_inf(m, SdMmc0Clk, ClkDiv     );
    sprintf_clk_inf(m, SdMmc0Clk, ClkPreDiv  );
    sprintf_clk_inf(m, SdMmc0Clk, ClkSrc     );
    sprintf_clk_inf(m, SdMmc0Clk, SpecClkGate);

    seq_printf(m, "\nSdMmc1Clk clk infor:\n");
    sprintf_clk_inf(m, SdMmc1Clk, ClkDiv     );
    sprintf_clk_inf(m, SdMmc1Clk, ClkPreDiv  );
    sprintf_clk_inf(m, SdMmc1Clk, ClkSrc     );
    sprintf_clk_inf(m, SdMmc1Clk, SpecClkGate);

    seq_printf(m, "\nSdMmc2Clk clk infor:\n");
    sprintf_clk_inf(m, SdMmc2Clk, ClkDiv     );
    sprintf_clk_inf(m, SdMmc2Clk, ClkPreDiv  );
    sprintf_clk_inf(m, SdMmc2Clk, ClkSrc     );
    sprintf_clk_inf(m, SdMmc2Clk, SpecClkGate);

    seq_printf(m, "\nSdMmc3Clk clk infor:\n");
    sprintf_clk_inf(m, SdMmc3Clk, ClkDiv     );
    sprintf_clk_inf(m, SdMmc3Clk, ClkPreDiv  );
    sprintf_clk_inf(m, SdMmc3Clk, ClkSrc     );
    sprintf_clk_inf(m, SdMmc3Clk, SpecClkGate);

    seq_printf(m, "\nTsClk clk infor:\n");
    sprintf_clk_inf(m, TsClk, ClkDiv         );
    sprintf_clk_inf(m, TsClk, ClkPreDiv      );
    sprintf_clk_inf(m, TsClk, ClkSrc         );
    sprintf_clk_inf(m, TsClk, SpecClkGate    );

    seq_printf(m, "\nSsClk clk infor:\n");
    sprintf_clk_inf(m, SsClk, ClkDiv         );
    sprintf_clk_inf(m, SsClk, ClkPreDiv      );
    sprintf_clk_inf(m, SsClk, ClkSrc         );
    sprintf_clk_inf(m, SsClk, SpecClkGate    );

    seq_printf(m, "\nSpi0Clk clk infor:\n");
    sprintf_clk_inf(m, Spi0Clk, ClkDiv       );
    sprintf_clk_inf(m, Spi0Clk, ClkPreDiv    );
    sprintf_clk_inf(m, Spi0Clk, ClkSrc       );
    sprintf_clk_inf(m, Spi0Clk, SpecClkGate  );

    seq_printf(m, "\nSpi1Clk clk infor:\n");
    sprintf_clk_inf(m, Spi1Clk, ClkDiv       );
    sprintf_clk_inf(m, Spi1Clk, ClkPreDiv    );
    sprintf_clk_inf(m, Spi1Clk, ClkSrc       );
    sprintf_clk_inf(m, Spi1Clk, SpecClkGate  );

    seq_printf(m, "\nSpi2Clk clk infor:\n");
    sprintf_clk_inf(m, Spi2Clk, ClkDiv       );
    sprintf_clk_inf(m, Spi2Clk, ClkPreDiv    );
    sprintf_clk_inf(m, Spi2Clk, ClkSrc       );
    sprintf_clk_inf(m, Spi2Clk, SpecClkGate  );

    seq_printf(m, "\nPataClk clk infor:\n");
    sprintf_clk_inf(m, PataClk, ClkDiv       );
    sprintf_clk_inf(m, PataClk, ClkPreDiv    );
    sprintf_clk_inf(m, PataClk, ClkSrc       );
    sprintf_clk_inf(m, PataClk, SpecClkGate  );

    seq_printf(m, "\nIr0Clk clk infor:\n");
    sprintf_clk_inf(m, Ir0Clk, ClkDiv        );
    sprintf_clk_inf(m, Ir0Clk, ClkPreDiv     );
    sprintf_clk_inf(m, Ir0Clk, ClkSrc        );
    sprintf_clk_inf(m, Ir0Clk, SpecClkGate   );

    seq_printf(m, "\nIr1Clk clk infor:\n");
    sprintf_clk_inf(m, Ir1Clk, ClkDiv        );
    sprintf_clk_inf(m, Ir1Clk, ClkPreDiv     );
    sprintf_clk_inf(m, Ir1Clk, ClkSrc        );
    sprintf_clk_inf(m, Ir1Clk, SpecClkGate   );

    seq_printf(m, "\nI2sClk clk infor:\n");
    sprintf_clk_inf(m, I2sClk, ClkDiv        );
    sprintf_clk_inf(m, I2sClk, SpecClkGate   );


    seq_printf(m, "\nAc97Clk clk infor:\n");
    sprintf_clk_inf(m, Ac97Clk, ClkDiv       );
    sprintf_clk_inf(m, Ac97Clk, SpecClkGate  );

    seq_printf(m, "\nSpdifClk clk infor:\n");
    sprintf_clk_inf(m, SpdifClk, ClkDiv      );
    sprintf_clk_inf(m, SpdifClk, SpecClkGate );

    seq_printf(m, "\nKeyPadClk clk infor:\n");
    sprintf_clk_inf(m, KeyPadClk, ClkDiv         );
    sprintf_clk_inf(m, KeyPadClk, ClkPreDiv      );
    sprintf_clk_inf(m, KeyPadClk, ClkSrc         );
    sprintf_clk_inf(m, KeyPadClk, SpecClkGate    );

    seq_printf(m, "\nSataClk clk infor:\n");
    sprintf_clk_inf(m, SataClk, ClkSrc       );
    sprintf_clk_inf(m, SataClk, SpecClkGate  );

    seq_printf(m, "\nUsbClk clk infor:\n");
    sprintf_clk_inf(m, UsbClk, UsbPhy0Rst        );
    sprintf_clk_inf(m, UsbClk, UsbPhy1Rst        );
    sprintf_clk_inf(m, UsbClk, UsbPhy2Rst        );
    sprintf_clk_inf(m, UsbClk, OHCIClkSrc        );
    sprintf_clk_inf(m, UsbClk, OHCI0SpecClkGate  );
    sprintf_clk_inf(m, UsbClk, OHCI1SpecClkGate  );
    sprintf_clk_inf(m, UsbClk, PhySpecClkGate    );

    seq_printf(m, "\nGpsClk clk infor:\n");
    sprintf_clk_inf(m, GpsClk, Reset         );
    sprintf_clk_inf(m, GpsClk, SpecClkGate   );

    seq_printf(m, "\nSpi3Clk clk infor:\n");
    sprintf_clk_inf(m, Spi3Clk, ClkDiv       );
    sprintf_clk_inf(m, Spi3Clk, ClkPreDiv    );
    sprintf_clk_inf(m, Spi3Clk, ClkSrc       );
    sprintf_clk_inf(m, Spi3Clk, SpecClkGate  );

    seq_printf(m, "\nDramGate clk infor:\n");
    sprintf_clk_inf(m, DramGate, VeGate      );
    sprintf_clk_inf(m, DramGate, Csi0Gate    );
    sprintf_clk_inf(m, DramGate, Csi1Gate    );
    sprintf_clk_inf(m, DramGate, TsGate      );
    sprintf_clk_inf(m, DramGate, TvdGate     );
    sprintf_clk_inf(m, DramGate, Tve0Gate    );
    sprintf_clk_inf(m, DramGate, Tve1Gate    );
    sprintf_clk_inf(m, DramGate, ClkOutputEn );
    sprintf_clk_inf(m, DramGate, DeFe0Gate   );
    sprintf_clk_inf(m, DramGate, DeFe1Gate   );
    sprintf_clk_inf(m, DramGate, DeBe0Gate   );
    sprintf_clk_inf(m, DramGate, DeBe1Gate   );
    sprintf_clk_inf(m, DramGate, DeMpGate    );
    sprintf_clk_inf(m, DramGate, AceGate     );

    seq_printf(m, "\nDeBe0Clk clk infor:\n");
    sprintf_clk_inf(m, DeBe0Clk, ClkDiv      );
    sprintf_clk_inf(m, DeBe0Clk, ClkSrc      );
    sprintf_clk_inf(m, DeBe0Clk, Reset       );
    sprintf_clk_inf(m, DeBe0Clk, SpecClkGate );

    seq_printf(m, "\nDeBe1Clk clk infor:\n");
    sprintf_clk_inf(m, DeBe1Clk, ClkDiv      );
    sprintf_clk_inf(m, DeBe1Clk, ClkSrc      );
    sprintf_clk_inf(m, DeBe1Clk, Reset       );
    sprintf_clk_inf(m, DeBe1Clk, SpecClkGate );

    seq_printf(m, "\nDeFe0Clk clk infor:\n");
    sprintf_clk_inf(m, DeFe0Clk, ClkDiv      );
    sprintf_clk_inf(m, DeFe0Clk, ClkSrc      );
    sprintf_clk_inf(m, DeFe0Clk, Reset       );
    sprintf_clk_inf(m, DeFe0Clk, SpecClkGate );

    seq_printf(m, "\nDeFe1Clk clk infor:\n");
    sprintf_clk_inf(m, DeFe1Clk, ClkDiv      );
    sprintf_clk_inf(m, DeFe1Clk, ClkSrc      );
    sprintf_clk_inf(m, DeFe1Clk, Reset       );
    sprintf_clk_inf(m, DeFe1Clk, SpecClkGate );

    seq_printf(m, "\nDeMpClk clk infor:\n");
    sprintf_clk_inf(m, DeMpClk, ClkDiv       );
    sprintf_clk_inf(m, DeMpClk, ClkSrc       );
    sprintf_clk_inf(m, DeMpClk, Reset        );
    sprintf_clk_inf(m, DeMpClk, SpecClkGate  );

    seq_printf(m, "\nLcd0Ch0Clk clk infor:\n");
    sprintf_clk_inf(m, Lcd0Ch0Clk, ClkSrc        );
    sprintf_clk_inf(m, Lcd0Ch0Clk, Reset         );
    sprintf_clk_inf(m, Lcd0Ch0Clk, SpecClkGate   );

    seq_printf(m, "\nLcd1Ch0Clk clk infor:\n");
    sprintf_clk_inf(m, Lcd1Ch0Clk, ClkSrc        );
    sprintf_clk_inf(m, Lcd1Ch0Clk, Reset         );
    sprintf_clk_inf(m, Lcd1Ch0Clk, SpecClkGate   );

    seq_printf(m, "\nCsiIspClk clk infor:\n");
    sprintf_clk_inf(m, CsiIspClk, ClkDiv         );
    sprintf_clk_inf(m, CsiIspClk, ClkSrc         );
    sprintf_clk_inf(m, CsiIspClk, SpecClkGate    );

    seq_printf(m, "\nTvdClk clk infor:\n");
    sprintf_clk_inf(m, TvdClk, ClkSrc        );
    sprintf_clk_inf(m, TvdClk, SpecClkGate   );

    seq_printf(m, "\nLcd0Ch1Clk clk infor:\n");
    sprintf_clk_inf(m, Lcd0Ch1Clk, ClkDiv        );
    sprintf_clk_inf(m, Lcd0Ch1Clk, SpecClk1Src   );
    sprintf_clk_inf(m, Lcd0Ch1Clk, SpecClk1Gate  );
    sprintf_clk_inf(m, Lcd0Ch1Clk, SpecClk2Src   );
    sprintf_clk_inf(m, Lcd0Ch1Clk, SpecClk2Gate  );

    seq_printf(m, "\nLcd1Ch1Clk clk infor:\n");
    sprintf_clk_inf(m, Lcd1Ch1Clk, ClkDiv        );
    sprintf_clk_inf(m, Lcd1Ch1Clk, SpecClk1Src   );
    sprintf_clk_inf(m, Lcd1Ch1Clk, SpecClk1Gate  );
    sprintf_clk_inf(m, Lcd1Ch1Clk, SpecClk2Src   );
    sprintf_clk_inf(m, Lcd1Ch1Clk, SpecClk2Gate  );

    seq_printf(m, "\nCsi0Clk clk infor:\n");
    sprintf_clk_inf(m, Csi0Clk, ClkDiv       );
    sprintf_clk_inf(m, Csi0Clk, ClkSrc       );
    sprintf_clk_inf(m, Csi0Clk, Reset        );
    sprintf_clk_inf(m, Csi0Clk, SpecClkGate  );

    seq_printf(m, "\nCsi1Clk clk infor:\n");
    sprintf_clk_inf(m, Csi1Clk, ClkDiv       );
    sprintf_clk_inf(m, Csi1Clk, ClkSrc       );
    sprintf_clk_inf(m, Csi1Clk, Reset        );
    sprintf_clk_inf(m, Csi1Clk, SpecClkGate  );

    seq_printf(m, "\nVeClk clk infor:\n");
    sprintf_clk_inf(m, VeClk, Reset          );
    sprintf_clk_inf(m, VeClk, ClkDiv         );
    sprintf_clk_inf(m, VeClk, SpecClkGate    );

    seq_printf(m, "\nAddaClk clk infor:\n");
    sprintf_clk_inf(m, AddaClk, SpecClkGate  );

    seq_printf(m, "\nAvsClk clk infor:\n");
    sprintf_clk_inf(m, AvsClk, SpecClkGate   );

    seq_printf(m, "\nAceClk clk infor:\n");
    sprintf_clk_inf(m, AceClk, ClkDiv        );
    sprintf_clk_inf(m, AceClk, Reset         );
    sprintf_clk_inf(m, AceClk, ClkSrc        );
    sprintf_clk_inf(m, AceClk, SpecClkGate   );

    seq_printf(m, "\nLvdsClk clk infor:\n");
    sprintf_clk_inf(m, LvdsClk, Reset        );

    seq_printf(m, "\nHdmiClk clk infor:\n");
    sprintf_clk_inf(m, HdmiClk, ClkDiv       );
    sprintf_clk_inf(m, HdmiClk, ClkSrc       );
    sprintf_clk_inf(m, HdmiClk, SpecClkGate  );

    seq_printf(m, "\nMaliClk clk infor:\n");
    sprintf_clk_inf(m, MaliClk, ClkDiv       );
    sprintf_clk_inf(m, MaliClk, ClkSrc       );
    sprintf_clk_inf(m, MaliClk, Reset        );
    sprintf_clk_inf(m, MaliClk, SpecClkGate  );

	return 0;
}


static int ccmu_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, ccmu_stats_show, NULL);
}

static const struct file_operations ccmu_dbg_fops = {
	.owner = THIS_MODULE,
	.open = ccmu_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init ccu_dbg_init(void)
{
	proc_create("ccmu", S_IRUGO, NULL, &ccmu_dbg_fops);
	return 0;
}

static void  __exit ccu_dbg_exit(void)
{
	remove_proc_entry("ccmu", NULL);
}

core_initcall(ccu_dbg_init);
module_exit(ccu_dbg_exit);
#endif

