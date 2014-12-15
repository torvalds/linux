#ifndef MIPI_DSI_PHY_REG
#define MIPI_DSI_PHY_REG
#define MIPI_DSI_PHY_START      0xd0150000
#define MIPI_DSI_PHY_END        0xd015ffff

#define MIPI_DSI_PHY_CTRL       0x0
  //bit 31.  soft reset for the phy. 1 = reset. 0 = dessert the reset.
  //bit 30.  clock lane soft reset.
  //bit 29.  data byte lane 3 soft reset.
  //bit 28.  data byte lane 2 soft reset.
  //bit 27.  data byte lane 1 soft reset.
  //bit 26.  data byte lane 0 soft reset.
  //bit 25.   mipi dsi pll clock selection.   1:  clock from fixed 850Mhz clock source. 0: from VID2 PLL.
  //bit 12.   mipi HSbyteclk enable.
  //bit 11.   mipi divider clk selection.  1: select the mipi DDRCLKHS from clock divider.  0: from PLL clock.
  //bit 10.   mipi clock divider control. 1 : /4. 0: /2.
  //bit 9.    mipi divider output enable.
  //bit 8.    mipi divider counter enable.
  //bit 7.   PLL clock enable.
  //bit 5.   LPDT data endian.  1 = transfer the high bit first. 0 : transfer the low bit first.
  //bit 4.   HS data endian.
  //bit 3.  force data byte lane in stop mode.
  //bit 2.  force data byte lane 0 in reciever mode.
  //bit 1. write 1 to sync the txclkesc input. the internal logic have to use txclkesc to decide Txvalid and Txready.
  //bit 0.  enalbe the MIPI DSI PHY TxDDRClk.

#define MIPI_DSI_CHAN_CTRL      0x1
  //bit 31.   clk lane tx_hs_en control selection.  1 = from register. 0 use clk lane state machine.
  //bit 30.   register bit for clock lane tx_hs_en.
  //bit 29.  clk lane tx_lp_en contrl selection.  1 = from register. 0 from clk lane state machine.
  //bit 28.  register bit for clock lane tx_lp_en.
  //bit 27.  chan0 tx_hs_en control selection. 1 = from register. 0 from chan0 state machine.
  //bit 26.  register bit for chan0 tx_hs_en.
  //bit 25.  chan0 tx_lp_en control selection. 1 = from register. 0 from chan0 state machine.
  //bit 24. register bit from chan0 tx_lp_en.
  //bit 23.  chan0 rx_lp_en control selection. 1 = from register. 0 from chan0 state machine.
  //bit 22. register bit from chan0 rx_lp_en.
  //bit 21.  chan0 contention detection enable control selection. 1 = from register. 0 from chan0 state machine.
  //bit 20. register bit from chan0 contention dectection enable.
  //bit 19.  chan1 tx_hs_en control selection. 1 = from register. 0 from chan0 state machine.
  //bit 18.  register bit for chan1 tx_hs_en.
  //bit 17.  chan1 tx_lp_en control selection. 1 = from register. 0 from chan0 state machine.
  //bit 16. register bit from chan1 tx_lp_en.
  //bit 15.  chan2 tx_hs_en control selection. 1 = from register. 0 from chan0 state machine.
  //bit 14.  register bit for chan2 tx_hs_en.
  //bit 13.  chan2 tx_lp_en control selection. 1 = from register. 0 from chan0 state machine.
  //bit 12. register bit from chan2 tx_lp_en.
  //bit 11. chan3 tx_hs_en control selection. 1 = from register. 0 from chan0 state machine.
  //bit 10. register bit for chan3 tx_hs_en.
  //bit 9.  chan3 tx_lp_en control selection. 1 = from register. 0 from chan0 state machine.
  //bit 8. register bit from chan3 tx_lp_en.
  //bit 4.  clk chan power down. this bit is also used as the power down of the whole MIPI_DSI_PHY.
  //bit 3.  chan3 power down.
  //bit 2.  chan2 power down.
  //bit 1.  chan1 power down.
  //bit 0.  chan0 power down.
#define MIPI_DSI_CHAN_STS       0x2
 //bit 24.     rx turn watch dog triggered.
 //bit 23      rx esc watchdog  triggered.
  // bit 22    mbias ready.
  //bit 21     txclkesc  synced and ready.
 //bit 20:17  clk lane state. {mbias_ready, tx_stop, tx_ulps, tx_hs_active}
 //bit 16:13 chan3 state{0, tx_stop, tx_ulps, tx_hs_active}
  //bit 12:9 chan2 state.{0, tx_stop, tx_ulps, tx_hs_active}
 //bit 8:5  chan1 state. {0, tx_stop, tx_ulps, tx_hs_active}
 //bit 4:0  chan0 state. {TX_STOP, tx_ULPS, hs_active, direction, rxulpsesc}
#define MIPI_DSI_CLK_TIM        0x3
  //bit 31:24. TCLK_PREPARE.
  //bit 23:16. TCLK_ZERO.
  //bit 15:8.  TCLK_POST.
  //bit 7:0.   TCLK_TRAIL.
#define MIPI_DSI_HS_TIM         0x4
  //bit 31:24. THS_PREPARE.
  //bit 23:16. THS_ZERO.
  //bit 15:8.  THS_TRAIL.
  //bit 7:0.   THS_EXIT.
#define MIPI_DSI_LP_TIM         0x5
  //bit 31:24. tTA_GET.
  //bit 23:16. tTA_GO.
  //bit 15:8.  tTA_SURE.
  //bit 7:0.   tLPX.
#define MIPI_DSI_ANA_UP_TIM     0x6
  //wait time to  MIPI DIS analog ready.
#define MIPI_DSI_INIT_TIM       0x7
  // TINIT.
#define MIPI_DSI_WAKEUP_TIM     0x8
  //TWAKEUP.
#define MIPI_DSI_LPOK_TIM       0x9
  //when in RxULPS check state, after the the logic enable the analog, how long we should wait to check the lP state .
#define MIPI_DSI_LP_WCHDOG      0xa
  //Watchdog for RX low power state no finished.
#define MIPI_DSI_ANA_CTRL       0xb
  //tMBIAS,  after send power up signals to analog, how long we should wait for analog powered up.
#define MIPI_DSI_CLK_TIM1       0xc
  //bit 31:8.  reserved for future.
  //bit 7:0.   tCLK_PRE.
#define MIPI_DSI_TURN_WCHDOG    0xd
   //watchdog for turn around waiting time.
#define MIPI_DSI_ULPS_CHECK     0xe
   //When in RxULPS state, how frequency we should to check if the TX side out of ULPS state.
#endif
