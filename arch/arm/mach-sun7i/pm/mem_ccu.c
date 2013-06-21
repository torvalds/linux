#include "pm_types.h"
#include "pm_i.h"

static __ccmu_reg_list_t   CmuReg;
/*
*********************************************************************************************************
*                                       MEM CCU INITIALISE
*
* Description: mem interrupt initialise.
*
* Arguments  : none.
*
* Returns    : 0/-1;
*********************************************************************************************************
*/
__s32 mem_ccu_save(__ccmu_reg_list_t *pReg)
{
	// CmuReg.Pll1Ctl      = pReg->Pll1Ctl;
	// CmuReg.Pll1Tune     = pReg->Pll1Tune;
	CmuReg.Pll2Ctl      = pReg->Pll2Ctl;
	//CmuReg.Pll2Tune     = pReg->Pll2Tune;
	CmuReg.Pll3Ctl      = pReg->Pll3Ctl;
	CmuReg.Pll4Ctl      = pReg->Pll4Ctl;
	// CmuReg.Pll5Ctl      = pReg->Pll5Ctl;
	// CmuReg.Pll5Tune     = pReg->Pll5Tune;
	CmuReg.Pll6Ctl      = pReg->Pll6Ctl;
	//CmuReg.Pll6Tune     = pReg->Pll6Tune;
	CmuReg.Pll7Ctl      = pReg->Pll7Ctl;
	//CmuReg.Pll7Tune     = pReg->Pll7Tune;
	// CmuReg.Pll1Tune2    = pReg->Pll1Tune2;
	// CmuReg.Pll5Tune2    = pReg->Pll5Tune2;
	CmuReg.Pll8Ctl	= pReg->Pll8Ctl;
	
	CmuReg.HoscCtl      = pReg->HoscCtl;
	CmuReg.SysClkDiv    = pReg->SysClkDiv;
	CmuReg.Apb1ClkDiv   = pReg->Apb1ClkDiv;
	
	CmuReg.AhbGate0     = pReg->AhbGate0;
	CmuReg.AhbGate1     = pReg->AhbGate1;
	CmuReg.Apb0Gate     = pReg->Apb0Gate;
	CmuReg.Apb1Gate     = pReg->Apb1Gate;
	
	CmuReg.NandClk      = pReg->NandClk;
	CmuReg.MsClk        = pReg->MsClk;
	CmuReg.SdMmc0Clk    = pReg->SdMmc0Clk;
	CmuReg.SdMmc1Clk    = pReg->SdMmc1Clk;
	CmuReg.SdMmc2Clk    = pReg->SdMmc2Clk;
	CmuReg.SdMmc3Clk    = pReg->SdMmc3Clk;
	CmuReg.TsClk        = pReg->TsClk;
	CmuReg.SsClk        = pReg->SsClk;
	CmuReg.Spi0Clk      = pReg->Spi0Clk;
	CmuReg.Spi1Clk      = pReg->Spi1Clk;
	CmuReg.Spi2Clk      = pReg->Spi2Clk;
	CmuReg.PataClk      = pReg->PataClk;
	CmuReg.Ir0Clk       = pReg->Ir0Clk;
	CmuReg.Ir1Clk       = pReg->Ir1Clk;
	CmuReg.I2s0Clk       = pReg->I2s0Clk;
	CmuReg.Ac97Clk      = pReg->Ac97Clk;
	CmuReg.SpdifClk     = pReg->SpdifClk;
	CmuReg.KeyPadClk    = pReg->KeyPadClk;
	CmuReg.SataClk      = pReg->SataClk;
	CmuReg.UsbClk       = pReg->UsbClk;
//	CmuReg.GpsClk       = pReg->GpsClk;
	CmuReg.Spi3Clk      = pReg->Spi3Clk;
	CmuReg.I2s1Clk      = pReg->I2s1Clk;
	CmuReg.I2s2Clk      = pReg->I2s2Clk;
	CmuReg.DramGate     = pReg->DramGate;
	CmuReg.DeBe0Clk     = pReg->DeBe0Clk;
	CmuReg.DeBe1Clk     = pReg->DeBe1Clk;
	CmuReg.DeFe0Clk     = pReg->DeFe0Clk;
	CmuReg.DeFe1Clk     = pReg->DeFe1Clk;
	CmuReg.DeMpClk      = pReg->DeMpClk;
	CmuReg.Lcd0Ch0Clk   = pReg->Lcd0Ch0Clk;
	CmuReg.Lcd1Ch0Clk   = pReg->Lcd1Ch0Clk;
	CmuReg.CsiIspClk    = pReg->CsiIspClk;
	CmuReg.TvdClk       = pReg->TvdClk;
	CmuReg.Lcd0Ch1Clk   = pReg->Lcd0Ch1Clk;
	CmuReg.Lcd1Ch1Clk   = pReg->Lcd1Ch1Clk;
	CmuReg.Csi0Clk      = pReg->Csi0Clk;
	CmuReg.Csi1Clk      = pReg->Csi1Clk;
	CmuReg.VeClk        = pReg->VeClk;
	CmuReg.AddaClk      = pReg->AddaClk;
	CmuReg.AvsClk       = pReg->AvsClk;
	CmuReg.AceClk       = pReg->AceClk;
	CmuReg.LvdsClk      = pReg->LvdsClk;
	CmuReg.HdmiClk      = pReg->HdmiClk;
	CmuReg.MaliClk      = pReg->MaliClk;

	CmuReg.MBusClk      = pReg->MBusClk;
	CmuReg.GmacClk      = pReg->GmacClk;
	CmuReg.ClkOutA      = pReg->ClkOutA;
	CmuReg.ClkOutB      = pReg->ClkOutB;
	
	return 0;
}

__s32 mem_ccu_restore(__ccmu_reg_list_t *pReg)
{
    //1. pll(pll1/pll5)
    pReg->Pll2Ctl       = CmuReg.Pll2Ctl;
    //pReg->Pll2Tune      = CmuReg.Pll2Tune;
    pReg->Pll3Ctl       = CmuReg.Pll3Ctl;
    pReg->Pll4Ctl       = CmuReg.Pll4Ctl;
    //pReg->Pll4Tune      = CmuReg.Pll4Tune;
    pReg->Pll6Ctl       = CmuReg.Pll6Ctl;
    //pReg->Pll6Tune      = CmuReg.Pll6Tune;
    pReg->Pll7Ctl       = CmuReg.Pll7Ctl;
    //pReg->Pll7Tune      = CmuReg.Pll7Tune;
    pReg->Pll8Ctl	= CmuReg.Pll8Ctl;

    //2. mod clk-src , div;
    pReg->HoscCtl       = CmuReg.HoscCtl;
    pReg->SysClkDiv     = CmuReg.SysClkDiv;
    pReg->Apb1ClkDiv    = CmuReg.Apb1ClkDiv;

    //3. mod gating;
    pReg->AhbGate0      = CmuReg.AhbGate0;
    pReg->AhbGate1      = CmuReg.AhbGate1;
    pReg->Apb0Gate      = CmuReg.Apb0Gate;
    pReg->Apb1Gate      = CmuReg.Apb1Gate;

    //4. mod reset;
    pReg->NandClk       = CmuReg.NandClk;
    pReg->MsClk         = CmuReg.MsClk;
    pReg->SdMmc0Clk     = CmuReg.SdMmc0Clk;
    pReg->SdMmc1Clk     = CmuReg.SdMmc1Clk;
    pReg->SdMmc2Clk     = CmuReg.SdMmc2Clk;
    pReg->SdMmc3Clk     = CmuReg.SdMmc3Clk;
    pReg->TsClk         = CmuReg.TsClk;
    pReg->SsClk         = CmuReg.SsClk;
    pReg->Spi0Clk       = CmuReg.Spi0Clk;
    pReg->Spi1Clk       = CmuReg.Spi1Clk;
    pReg->Spi2Clk       = CmuReg.Spi2Clk;
    pReg->PataClk       = CmuReg.PataClk;
    pReg->Ir0Clk        = CmuReg.Ir0Clk;
    pReg->Ir1Clk        = CmuReg.Ir1Clk;
    pReg->I2s0Clk        = CmuReg.I2s0Clk;
    pReg->Ac97Clk       = CmuReg.Ac97Clk;
    pReg->SpdifClk      = CmuReg.SpdifClk;
    pReg->KeyPadClk     = CmuReg.KeyPadClk;
    pReg->SataClk       = CmuReg.SataClk;
    pReg->UsbClk        = CmuReg.UsbClk;
//    pReg->GpsClk        = CmuReg.GpsClk;
    pReg->Spi3Clk       = CmuReg.Spi3Clk;
	pReg->I2s1Clk       = CmuReg.I2s1Clk;
	pReg->I2s2Clk       = CmuReg.I2s2Clk;
    pReg->DramGate      = CmuReg.DramGate;
    pReg->DeBe0Clk      = CmuReg.DeBe0Clk;
    pReg->DeBe1Clk      = CmuReg.DeBe1Clk;
    pReg->DeFe0Clk      = CmuReg.DeFe0Clk;
    pReg->DeFe1Clk      = CmuReg.DeFe1Clk;
    pReg->DeMpClk       = CmuReg.DeMpClk;
    pReg->Lcd0Ch0Clk    = CmuReg.Lcd0Ch0Clk;
    pReg->Lcd1Ch0Clk    = CmuReg.Lcd1Ch0Clk;
    pReg->CsiIspClk     = CmuReg.CsiIspClk;
    pReg->TvdClk        = CmuReg.TvdClk;
    pReg->Lcd0Ch1Clk    = CmuReg.Lcd0Ch1Clk;
    pReg->Lcd1Ch1Clk    = CmuReg.Lcd1Ch1Clk;
    pReg->Csi0Clk       = CmuReg.Csi0Clk;
    pReg->Csi1Clk       = CmuReg.Csi1Clk;
    pReg->VeClk         = CmuReg.VeClk;
    pReg->AddaClk       = CmuReg.AddaClk;
    pReg->AvsClk        = CmuReg.AvsClk;
    pReg->AceClk        = CmuReg.AceClk;
    pReg->LvdsClk       = CmuReg.LvdsClk;
    pReg->HdmiClk       = CmuReg.HdmiClk;
    pReg->MaliClk       = CmuReg.MaliClk;

    pReg->MBusClk	    = CmuReg.MBusClk;
    pReg->GmacClk	    = CmuReg.GmacClk;
    pReg->ClkOutA       = CmuReg.ClkOutA;
    pReg->ClkOutB       = CmuReg.ClkOutB;

	return 0;
}

