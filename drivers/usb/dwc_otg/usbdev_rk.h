
#define USB_PHY_ENABLED 0
#define USB_PHY_SUSPEND 1

#define PHY_USB_MODE    0
#define PHY_UART_MODE   1

#define USB_STATUS_BVABLID    1
#define USB_STATUS_DPDM	 	  2
#define USB_STATUS_ID         3
#define USB_STATUS_UARTMODE   4

struct dwc_otg_platform_data {
    void *privdata;
    struct clk* phyclk;
    struct clk* ahbclk;
    struct clk* busclk;
    int phy_status;
    void (*hw_init)(void);
    void (*phy_suspend)(void* pdata, int suspend);
    void (*soft_reset)(void);
    void (*clock_init)(void* pdata);
    void (*clock_enable)(void* pdata, int enable);
    void (*power_enable)(int enable);
    void (*dwc_otg_uart_mode)(void* pdata, int enter_usb_uart_mode);
    int (*get_status)(int id);
};
