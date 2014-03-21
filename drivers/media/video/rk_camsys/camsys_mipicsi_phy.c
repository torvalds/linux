#include "camsys_mipicsi_phy.h"

#if defined(CONFIG_ARCH_ROCKCHIP)
//GRF_SOC_CON14
//bit 0     dphy_rx0_testclr
//bit 1     dphy_rx0_testclk
//bit 2     dphy_rx0_testen
//bit 3:10 dphy_rx0_testdin
#define GRF_SOC_CON14_OFFSET    (0x027c)
#define DPHY_RX0_TESTCLR_MASK   (0x1<<16)
#define DPHY_RX0_TESTCLK_MASK   (0x1<<17)
#define DPHY_RX0_TESTEN_MASK    (0x1<<18)
#define DPHY_RX0_TESTDIN_MASK   (0xff<<19)

#define DPHY_RX0_TESTCLR    (1<<0)
#define DPHY_RX0_TESTCLK    (1<<1)
#define DPHY_RX0_TESTEN     (1<<2)
#define DPHY_RX0_TESTDIN_OFFSET    (3)


//GRF_SOC_CON6
//bit 0 grf_con_disable_isp
//bit 1 grf_con_isp_dphy_sel  1'b0 mipi phy rx0
#define GRF_SOC_CON6_OFFSET    (0x025c)
#define MIPI_PHY_RX0_MASK       (0x1<<16)
#define MIPI_PHY_RX0            (0x1<<0)

#endif

static void phy0_WriteReg(uint8_t addr, uint8_t data)
{

    //TESTEN =1,TESTDIN=addr
    write_grf_reg(GRF_SOC_CON14_OFFSET,(( addr << DPHY_RX0_TESTDIN_OFFSET) |DPHY_RX0_TESTDIN_MASK | DPHY_RX0_TESTEN| DPHY_RX0_TESTEN_MASK)); 
    //TESTCLK=0
    write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLK_MASK); 
    //TESTEN =0,TESTDIN=data
    write_grf_reg(GRF_SOC_CON14_OFFSET, (( data << DPHY_RX0_TESTDIN_OFFSET)|DPHY_RX0_TESTDIN_MASK |DPHY_RX0_TESTEN)); 
    //TESTCLK=1
    write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLK_MASK |DPHY_RX0_TESTCLK); 

}

static uint8_t phy0_ReadReg(uint8_t addr)  //read 0xff968034 bit8~15
{
    uint8_t data = 0;
    //TESTEN =1,TESTDIN=addr
    write_grf_reg(GRF_SOC_CON14_OFFSET,(( addr << DPHY_RX0_TESTDIN_OFFSET) |DPHY_RX0_TESTDIN_MASK | DPHY_RX0_TESTEN| DPHY_RX0_TESTEN_MASK)); 
    //TESTCLK=0
    write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLK_MASK); 

    data = ((read_grf_reg(GRF_SOC_CON14_OFFSET) >> DPHY_RX0_TESTDIN_OFFSET) & (0xff));

    camsys_err("%s phy addr = 0x%x,value = 0x%x\n",__func__,addr,data);
    return data ;

    
}


static void PHY0_Init(int numLane,int clkfreq)
{
    
//  select phy rx0
    write_grf_reg(GRF_SOC_CON6_OFFSET, MIPI_PHY_RX0_MASK | MIPI_PHY_RX0); 

//TESTCLK=1
    write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLK_MASK |DPHY_RX0_TESTCLK); 
//TESTCLR=1    
    write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLK_MASK |DPHY_RX0_TESTCLK | DPHY_RX0_TESTCLR_MASK |DPHY_RX0_TESTCLR);   
//TESTCLR=0  zyc
    write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLK_MASK |DPHY_RX0_TESTCLK); 

//**********************************************************************//

//set clock lane
    phy0_WriteReg(0x34,0x14);

//set lane 0
 /********************
    500-550M 0x0E
    600-650M 0x10
    720M     0x12
    360M     0x2A
    *******************/
    phy0_WriteReg(0x44,0x10);
 
 //**********************************************************************//

//Normal operation
    phy0_ReadReg(0x00);                           
    //TESTCLK=1
    write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLK_MASK |DPHY_RX0_TESTCLK); 

    //TESTEN =0
    write_grf_reg(GRF_SOC_CON14_OFFSET, (DPHY_RX0_TESTEN)); 

 }


static int camsys_mipiphy_ops (void *phy, void *phyinfo, unsigned int on)
{
    PHY0_Init(1,0);
    return 0;
}

static int camsys_mipiphy_clkin_cb(void *ptr, unsigned int on)
{
    return 0;
}

static int camsys_mipiphy_remove_cb(struct platform_device *pdev)
{
    return 0;
}
int camsys_mipiphy_probe_cb(struct platform_device *pdev, camsys_dev_t *camsys_dev)
{
 
    camsys_dev->mipiphy.clkin_cb = camsys_mipiphy_clkin_cb;
    camsys_dev->mipiphy.ops = camsys_mipiphy_ops;
    camsys_dev->mipiphy.remove = camsys_mipiphy_remove_cb;

    return 0;
  
}

