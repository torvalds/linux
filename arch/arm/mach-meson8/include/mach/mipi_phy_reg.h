#ifndef MIPI_PHY_REG
#define MIPI_PHY_REG
#include <mach/io.h>

#define MIPI_PHY_BASE                  IO_MIPI_PHY_BASE
#define mipi_phy_reg_wr(addr, data) *(volatile unsigned long *) (MIPI_PHY_BASE + (addr << 2) )=data
#define mipi_phy_reg_rd(addr) *(volatile unsigned long *) (MIPI_PHY_BASE + (addr << 2) )

#define MIPI_PHY_CTRL    		0x00
  //31:   soft reset.  set 1 will reset the MIPI phy cil_scnn and cil_sfen modules.
          // set 0 will release the reset.  it's level signal.
  //20:   if set, all analog control signals will directly from the related register bit.
  //19:18  mipi hs clock to pad selection.
           //2'b00 :  no output.
           //2'b01 :  output /2 clock.
           //2'b10 :  output /4 clock.
           //2'b11 :  output /8 clock.
  //17:15  mipi analog signal to pad selection.
           //3'b000: no output.
           //3'b001:  clock lane.
           //3'b010:  data lane 0.
           //3'b011:  data lane 1.
           //3'b100:  data lane 2.
           //3'b101:  data lane 3.
  //13     ddr to reg.   enalbe this bit the 8 interface DFFs result will be latch to
           // MIPI_PHY_DDR_STS registers.
  //12     enable this bit : all analog output signal will be latched to
           // MIPI_PHY_ANA_STS   registers.
  //11     not used. reserved for future..
  //10     force analog MBIAS enable.
  // 9:5    mipi_chpu  to analog.
  // 4      shut down digital clock lane.
  // 3      Shut down digital data lane 3.
  // 2      Shut down digital data lane 2.
  // 1      Shut down digital data lane 1.
  // 0      Shut down digital data lane 0.

#define MIPI_PHY_CLK_LANE_CTRL		0x01
  //11     force clock lane TH check enable.
  //10     force clock lane LP enable.
  //9      force clock lane HS RECEIVER enable  this signal is not used by analog.
  //8      force clock lane terminator enable
  //7       if set, will dislabe clock lane LPEN if clock lane is in HS mode.
            // if not set,  the LPEN is always enabled until in ULPS state.
  //6       force clock TCLK_ZERO check when in clock lane HS mode.
  //5:3     TCLK_ZERO timing check. check with the hs clock counter.
          //  000:  hs clock itself.
          //  001:   hs clock /2
          //  010:   hs clock /4
          //  011:   hs clock /8
          //  100:   hs clock /16
  // 1      force clock lane come out of ulps
  // 0      force clock lane enter ULPS state.



#define MIPI_PHY_DATA_LANE_CTRL		0x02
  //15 :   force data lane 3 THEN  enable.
  //14 :   force data lane 3 LP receiver enable.
  //13 :   force data lane 3 HS receiver enable.
  //12  :  force data lane 3 terminator enable.
  //11 :   force data lane 2 THEN  enable.
  //10 :   force data lane 2 LP receiver enable.
  //9 :    force data lane 2 HS receiver enable.
  //8 :    force data lane 2 terminator enable.
  //7 :    force data lane 1 THEN  enable.
  //6 :    force data lane 1 LP receiver enable.
  //5 :    force data lane 1 HS receiver enable.
  //4 :    force data lane 1 terminator enable.
  //3 :    force data lane 0 THEN  enable.
  //2 :    force data lane 0 LP receiver enable.
  //1 :    force data lane 0 HS receiver enable. // this bit is not used to control analog.
  //0 :    force data lane 0 terminator enable.

#define MIPI_PHY_DATA_LANE_CTRL1	0x03
   //12  LP data bit order.
   //11:10. HS data bit order.  2'b00.  low bit input early.
   //9:7    data pipe sel. output data use with pipe line data.
   //6:2.   these addition 5 pipe line to same the high speed data.
           //each bit for one pipe line.
   // 1    if set enable the hs_sync error bit check.
   // 0:   for CSI2, only ULPS command accepted. if set this bit, all other command will insert the            //ErrEsc signal.

#define MIPI_PHY_TCLK_MISS		0x04
#define MIPI_PHY_TCLK_SETTLE		0x05
#define MIPI_PHY_THS_EXIT		0x06
#define MIPI_PHY_THS_SKIP		0x07
#define MIPI_PHY_THS_SETTLE		0x08
#define MIPI_PHY_TINIT			0x09
#define MIPI_PHY_TULPS_C		0x0a
#define MIPI_PHY_TULPS_S		0x0b
#define MIPI_PHY_TMBIAS		        0x0c
   // how many cycles need to wait for analog MBIAS stable after MIPI_MBIAS_EN is inserted.
#define MIPI_PHY_TLP_EN_W		0x0d
   // how many cycles need to wait for analog LP receiver stable output after LPEN is inserted.
#define MIPI_PHY_TLPOK    		0x0e
   // how many cycles need to wait for analog LP receiver stable output after LPEN is inserted.
#define MIPI_PHY_TWD_INIT               0x0f
   // watch dog for init.
#define MIPI_PHY_TWD_HS                 0x10
   // watch dog for hs speed transfer.
#define MIPI_PHY_AN_CTRL0		0x11
#define MIPI_PHY_AN_CTRL1		0x12
#define MIPI_PHY_AN_CTRL2		0x13
#define MIPI_PHY_CLK_LANE_STS		0x14
  //3:0 clock lane states.
       // 4'h0 : Power_down state.
       // 4'h1 : POWER_UP state. //waiting for TINIT and MBIAS ready.
       // 4'h2 : INIT state  //waiting the input to STOP.
       // 4'h3 : STOP state.
       // 4'h4 : ULPS request state. after receiver the ulps request, waiting everything setlled.
       // 4'h5 : ULPS state.
       // 4'h6 : ULPS exit state. checked ULPS exit request and waiting for input in STOP.
       // 4'h7 : HS data transfer request state. LP = 2'b01:
       // 4'h8 : HS bridge state.     LP = 2'b00:
       // 4'h9 : HS CLK ZERO state.   enable the HS reciever in this stage the input clock is zero.
       // 4'ha : HS transfer state.
       // 4'hb : HS TRAIL state.  if detected no clock edge , the state machine will try to go to stop state.

#define MIPI_PHY_DATA_LANE0_STS		0x15
   //6:4 : data lane 0 HS sub state.  because this is across clock domain state. this is only for static debug.
   //3:0  data lane 0 state.
         //4'h0 : POWER_DOWN State.
         //4'h1 : POWER UP state.
         //4'h2 : INIT state.
         //4'h3 : STOP state.
         //4'h4 : HS REQUST state.
         //4'h5 : HS PREPARE state.
         //4'h6 : HS transfer state.
         //4'h7 : HS exit state.
         //4'h8 : ESC request state.
         //4'h9 : ESC bridge 0 state.
         //4'ha : ESC bridge 1 state.
         //4'hb : ESC command state.
         //4'hc : ESC EXIT state.
         //4'hd : LP data transfer state.
         //4'he : ULPS state.
         //4'hf : ULPS exit state.
#define MIPI_PHY_DATA_LANE1_STS		0x16
   //6:4 : data lane 0 HS sub state.  because this is across clock domain state. this is only for static debug.
   //3:0 : data lane 0 state.

#define MIPI_PHY_DATA_LANE2_STS		0x17
#define MIPI_PHY_DATA_LANE3_STS		0x18
#define MIPI_PHY_ESC_CMD		0x19
#define MIPI_PHY_INT_CTRL		0x1a
   //24:  read to clear the INT_STS.  when this bit is set, read MIPI_PHY_INT_STS will clean all interupt status bits.
   //18:0  each bit to enable related interrupt generate. if this bit is set, it will generate a interrupt to cpu when the interrupt source is triggered..
          // otherwise only change the status bit.
#define MIPI_PHY_INT_STS		0x1b
   //18    clock lane ulps exit interupt
   //17    clock lane ulps enter interrupt
   //16    clock lane initilization watch dog interrupt.
   //15    data  lane 3 initiliaztion watch dog interrupt.
   //14    data  lane 2 initiliaztion watch dog interrupt.
   //13    data  lane 1 initiliaztion watch dog interrupt.
   //12    data  lane 0 initiliaztion watch dog interrupt.
   //11    data  lane 3 HS transfer watch dog interrupt.
   //10    data  lane 2 HS transfer watch dog interrupt.
   //9     data  lane 1 HS transfer watch dog interrupt.
   //8     data  lane 0 HS transfer watch dog interrupt.
   //7     data  lane 3 HS transfer sync error interrupt.
   //6     data  lane 2 HS transfer sync error interrupt.
   //5     data  lane 1 HS transfer sync error interrupt.
   //4     data  lane 0 HS transfer sync error interrupt.
   //3     data  lane 3 ESC command ready interrupt.
   //2     data  lane 2 ESC command ready interrupt.
   //1     data  lane 1 ESC command ready interrupt.
   //0     data  lane 0 ESC command ready interrupt.

#define MIPI_PHY_ANA_STS                0x1c
#define MIPI_PHY_DDR_STS                0x1d

// MIPI-CSI2 host registers
#define MIPI_CSI2_HOST_VERSION          (0x000)
#define MIPI_CSI2_HOST_N_LANES          (0x001)
#define MIPI_CSI2_HOST_PHY_SHUTDOWNZ    (0x002)
#define MIPI_CSI2_HOST_DPHY_RSTZ        (0x003)
#define MIPI_CSI2_HOST_CSI2_RESETN      (0x004)
#define MIPI_CSI2_HOST_PHY_STATE        (0x005)
#define MIPI_CSI2_HOST_DATA_IDS_1       (0x006)
#define MIPI_CSI2_HOST_DATA_IDS_2       (0x007)
#define MIPI_CSI2_HOST_ERR1             (0x008)
#define MIPI_CSI2_HOST_ERR2             (0x009)
#define MIPI_CSI2_HOST_MASK1            (0x00A)
#define MIPI_CSI2_HOST_MASK2            (0x00B)
#define MIPI_CSI2_HOST_PHY_TST_CTRL0    (0x00C)
#define MIPI_CSI2_HOST_PHY_TST_CTRL1    (0x00D)

#endif
