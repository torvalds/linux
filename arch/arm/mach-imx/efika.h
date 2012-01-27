#ifndef _EFIKA_H
#define _EFIKA_H

#define EFIKA_WLAN_EN		IMX_GPIO_NR(2, 16)
#define EFIKA_WLAN_RESET	IMX_GPIO_NR(2, 10)
#define EFIKA_USB_PHY_RESET	IMX_GPIO_NR(2, 9)

void __init efika_board_common_init(void);

#endif
