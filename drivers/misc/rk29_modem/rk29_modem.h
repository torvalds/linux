#include <linux/platform_device.h>
#include <mach/board.h>

/* Modem states */
#define MODEM_DISABLE       0
#define MODEM_ENABLE        1
#define MODEM_SLEEP         2
#define MODEM_WAKEUP        3
#define MODEM_MAX_STATUS    4

struct rk29_irq_t {
    unsigned long irq_addr;
    unsigned long irq_trigger;
};

struct rk29_modem_t {
    struct platform_driver *driver;
    // 控制modem电源的IO
    struct rk29_io_t *modem_power;
    // 当AP就绪或者未就绪时，通过 ap_ready 这个IO来通知BP。
    struct rk29_io_t *ap_ready;
    // 当BP接收到短信或者来电时，通过 bp_wakeup_ap 这个IRQ来唤醒AP
    struct rk29_irq_t *bp_wakeup_ap;
    // 当前modem状态，目前只用到MODEM_ENABLE(上电)、MODEM_DISABLE(下电)
    // 同时，status的初始值也决定开机时的modem是否上电
    int status;
    struct wake_lock wakelock_bbwakeupap;

    // 设备初始化函数, 主要设置各个GPIO以及IRQ的申请等
    int (*dev_init)(struct rk29_modem_t *driver);
    int (*dev_uninit)(struct rk29_modem_t *driver);
    irqreturn_t (*irq_handler)(int irq, void *dev_id);
    int (*suspend)(struct platform_device *pdev, pm_message_t state);
    int (*resume)(struct platform_device *pdev);

    int (*enable)(struct rk29_modem_t *driver);
    int (*disable)(struct rk29_modem_t *driver);
    int (*sleep)(struct rk29_modem_t *driver);
    int (*wakeup)(struct rk29_modem_t *driver);
};

void rk29_modem_exit(void);
int rk29_modem_init(struct rk29_modem_t *rk29_modem);
int rk29_modem_suspend(struct platform_device *pdev, pm_message_t state);
int rk29_modem_resume(struct platform_device *pdev);

