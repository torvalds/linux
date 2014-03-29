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

#define DPHY_TX1RX1_ENABLECLK_MASK   (0x1<<28)
#define DPHY_RX1_SRC_SEL_MASK        (0x1<<29)
#define DPHY_TX1RX1_MASTERSLAVEZ_MASK (0x1<<30)
#define DPHY_TX1RX1_BASEDIR_OFFSET  (0x1<<31)

#define DPHY_TX1RX1_ENABLECLK           (0x1<<12)
#define DPHY_TX1RX1_DISABLECLK          (0x0<<12)
#define DPHY_RX1_SRC_SEL_ISP          (0x1<<13)
#define DPHY_TX1RX1_SLAVEZ            (0x0<<14)
#define DPHY_TX1RX1_BASEDIR_REC       (0x1<<15)



//GRF_SOC_CON6
//bit 0 grf_con_disable_isp
//bit 1 grf_con_isp_dphy_sel  1'b0 mipi phy rx0
#define GRF_SOC_CON6_OFFSET    (0x025c)
#define MIPI_PHY_DISABLE_ISP_MASK       (0x1<<16)
#define MIPI_PHY_DISABLE_ISP            (0x0<<0)

#define DSI_CSI_TESTBUS_SEL_MASK        (0x1<<30)
#define DSI_CSI_TESTBUS_SEL_OFFSET_BIT  (14)


#define MIPI_PHY_DPHYSEL_OFFSET_MASK (0x1<<17)
#define MIPI_PHY_DPHYSEL_OFFSET_BIT (0x1)

//GRF_SOC_CON10
//bit12:15 grf_dphy_rx0_enable
//bit 0:3 turn disable
#define GRF_SOC_CON10_OFFSET                (0x026c)
#define DPHY_RX0_TURN_DISABLE_MASK          (0xf<<16)
#define DPHY_RX0_TURN_DISABLE_OFFSET_BITS   (0x0)
#define DPHY_RX0_ENABLE_MASK                (0xf<<28)
#define DPHY_RX0_ENABLE_OFFSET_BITS         (12)

//GRF_SOC_CON9
//bit12:15 grf_dphy_rx0_enable
//bit 0:3 turn disable
#define GRF_SOC_CON9_OFFSET                (0x0268)
#define DPHY_TX1RX1_TURN_DISABLE_MASK          (0xf<<16)
#define DPHY_TX1RX1_TURN_DISABLE_OFFSET_BITS   (0x0)
#define DPHY_TX1RX1_ENABLE_MASK                (0xf<<28)
#define DPHY_TX1RX1_ENABLE_OFFSET_BITS         (12)

//GRF_SOC_CON15
//bit 0:3   turn request
#define GRF_SOC_CON15_OFFSET                (0x03a4) 
#define DPHY_RX0_TURN_REQUEST_MASK          (0xf<<16)
#define DPHY_RX0_TURN_REQUEST_OFFSET_BITS   (0x0)

#define DPHY_TX1RX1_TURN_REQUEST_MASK          (0xf<<20)
#define DPHY_TX1RX1_TURN_REQUEST_OFFSET_BITS   (0x0)


#endif


static void phy_select(uint8_t index)
{
    if((index == 0) || (index == 1)){
        write_grf_reg(GRF_SOC_CON6_OFFSET, MIPI_PHY_DPHYSEL_OFFSET_MASK | (index<<MIPI_PHY_DPHYSEL_OFFSET_BIT)); 
        if(index == 1){
            write_grf_reg(GRF_SOC_CON6_OFFSET, DSI_CSI_TESTBUS_SEL_MASK | (1<<DSI_CSI_TESTBUS_SEL_OFFSET_BIT)); 

            write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX1_SRC_SEL_ISP | DPHY_RX1_SRC_SEL_MASK); 
            write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_TX1RX1_SLAVEZ | DPHY_TX1RX1_MASTERSLAVEZ_MASK); 
            write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_TX1RX1_BASEDIR_REC | DPHY_TX1RX1_BASEDIR_OFFSET); 
        }

    }else{
        camsys_err("phy index is erro!");
        
    }
}


static void phy0_WriteReg(uint8_t addr, uint8_t data)
{
   // uint8_t test_data = 0;
    //TESTEN =1,TESTDIN=addr
    write_grf_reg(GRF_SOC_CON14_OFFSET,(( addr << DPHY_RX0_TESTDIN_OFFSET) |DPHY_RX0_TESTDIN_MASK | DPHY_RX0_TESTEN| DPHY_RX0_TESTEN_MASK)); 
//	//TESTCLK=1
    write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLK_MASK |DPHY_RX0_TESTCLK);
    
    //TESTCLK=0
	 write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLK_MASK); 
  
    if(data != -1){ //write data ?
    	 //TESTEN =0,TESTDIN=data
        write_grf_reg(GRF_SOC_CON14_OFFSET, (( data << DPHY_RX0_TESTDIN_OFFSET)|DPHY_RX0_TESTDIN_MASK |DPHY_RX0_TESTEN)); 

        //TESTCLK=1
        write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLK_MASK |DPHY_RX0_TESTCLK); 
    }
}

static uint8_t phy0_ReadReg(uint8_t addr)  
{
    uint8_t data = 0;
    
    //TESTEN =1,TESTDIN=addr
    write_grf_reg(GRF_SOC_CON14_OFFSET,(( addr << DPHY_RX0_TESTDIN_OFFSET) |DPHY_RX0_TESTDIN_MASK | DPHY_RX0_TESTEN| DPHY_RX0_TESTEN_MASK)); 

    //TESTCLK=0
    write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLK_MASK); 

    return data ;

    
}

static void phy_config_num_lane(uint8_t index,int numLane)
{
    uint8_t lane_mask =0;
    int i = 0;

    for(i=0;i<numLane;i++){
        lane_mask |= 1<<i;
    }
    camsys_trace(1,"lane num = 0x%d\n",lane_mask);
    if(index == 0){
    //  set lane num
        write_grf_reg(GRF_SOC_CON10_OFFSET, DPHY_RX0_ENABLE_MASK | (lane_mask << DPHY_RX0_ENABLE_OFFSET_BITS)); 
    //  set lan turndisab as 1
        write_grf_reg(GRF_SOC_CON10_OFFSET, DPHY_RX0_TURN_DISABLE_MASK | (0xf << DPHY_RX0_TURN_DISABLE_OFFSET_BITS));

        write_grf_reg(GRF_SOC_CON10_OFFSET, (0xc<<4)|(0xf<<20));

    //  set lan turnrequest as 0   
        write_grf_reg(GRF_SOC_CON15_OFFSET, DPHY_RX0_TURN_REQUEST_MASK | (0x0 << DPHY_RX0_TURN_REQUEST_OFFSET_BITS));
    }else if(index == 1){
    //  set lane num
        write_grf_reg(GRF_SOC_CON9_OFFSET, DPHY_TX1RX1_ENABLE_MASK | (lane_mask << DPHY_TX1RX1_ENABLE_OFFSET_BITS)); 
    //  set lan turndisab as 1
        write_grf_reg(GRF_SOC_CON9_OFFSET, DPHY_TX1RX1_TURN_DISABLE_MASK | (0xf << DPHY_TX1RX1_TURN_DISABLE_OFFSET_BITS));
    //  set lan turnrequest as 0   
        write_grf_reg(GRF_SOC_CON15_OFFSET, DPHY_TX1RX1_TURN_REQUEST_MASK | (0x0 << DPHY_TX1RX1_TURN_REQUEST_OFFSET_BITS));
    }
}

static void phy0_start(int freq,int numLane)
{
//TESTCLK=1
    write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLK_MASK |DPHY_RX0_TESTCLK); 
//TESTCLR=1    
    write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLR_MASK |DPHY_RX0_TESTCLR);   
//TESTCLR=0  zyc
//    write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLR_MASK); 
    udelay(1000);

//**********************************************************************//

#if 1
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
    if(numLane > 1)
        phy0_WriteReg(0x54,0x10);
#endif
 
 //**********************************************************************//

//Normal operation
    phy0_WriteReg(0x0,-1);
    //TESTCLK=1
    write_grf_reg(GRF_SOC_CON14_OFFSET, DPHY_RX0_TESTCLK_MASK |DPHY_RX0_TESTCLK); 

    //TESTEN =0
    write_grf_reg(GRF_SOC_CON14_OFFSET, (DPHY_RX0_TESTEN_MASK)); 

}



static int camsys_mipiphy_ops (void *phy, void *phyinfo, unsigned int on)
{
#if 0
    camsys_phyinfo_t* phyinfo_s = (camsys_phyinfo_t*)phyinfo;
    struct camsys_mipiphy_s* phy_s = (struct camsys_mipiphy_s*)phy;
    if(phy_s->phy_index == 0){
        phy_select(phy_s->phy_index);
        phy_config_num_lane(phy_s->data_en_bit);
        phy0_start(0,phy_s->data_en_bit);

    }else if(phy_s->phy_index == 1){

    }else{

        camsys_err("phy index is erro!");
    }
#else
#if 0
    if(on == 1){
    //disable isp
        write_grf_reg(GRF_SOC_CON6_OFFSET, MIPI_PHY_DISABLE_ISP_MASK | 1); 
        phy_select(0);
    //    phy_config_num_lane(0,2);
        phy0_start(0,2);

        phy_config_num_lane(0,2);
        udelay(200);
    //enable isp
        write_grf_reg(GRF_SOC_CON6_OFFSET, MIPI_PHY_DISABLE_ISP_MASK | 0); 
    }else
#endif
    {
    //disable isp
        write_grf_reg(GRF_SOC_CON6_OFFSET, MIPI_PHY_DISABLE_ISP_MASK | 1); 
        phy_select(0);
        phy_config_num_lane(0,1);
        phy0_start(0,1);

        udelay(200);
    //enable isp
        write_grf_reg(GRF_SOC_CON6_OFFSET, MIPI_PHY_DISABLE_ISP_MASK | 0); 
    }

#endif   
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

