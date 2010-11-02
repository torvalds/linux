#ifdef CONFIG_ARCH_MX1
extern struct platform_device imx1_camera_device;
extern struct platform_device imx_rtc_device;
extern struct platform_device imx_wdt_device;
extern struct platform_device imx_usb_device;
#endif

#if defined(CONFIG_MACH_MX21) || defined(CONFIG_MACH_MX27)
extern struct platform_device mxc_gpt1;
extern struct platform_device mxc_gpt2;
#ifdef CONFIG_MACH_MX27
extern struct platform_device mxc_gpt3;
extern struct platform_device mxc_gpt4;
extern struct platform_device mxc_gpt5;
#endif
extern struct platform_device mxc_wdt;
extern struct platform_device mxc_w1_master_device;
extern struct platform_device mxc_fb_device;
extern struct platform_device mxc_pwm_device;
extern struct platform_device mxc_sdhc_device0;
extern struct platform_device mxc_sdhc_device1;
extern struct platform_device mxc_otg_udc_device;
extern struct platform_device mx27_camera_device;
extern struct platform_device mxc_otg_host;
extern struct platform_device mxc_usbh1;
extern struct platform_device mxc_usbh2;
extern struct platform_device mx21_usbhc_device;
extern struct platform_device imx_kpp_device;
#endif
