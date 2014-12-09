/*
 * Copyright (c) 2010 -2014 Espressif System.
 *
 * power save control of system
 */
#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include "esp_pub.h"

#ifdef CONFIG_HAS_EARLYSUSPEND

static void esp_early_suspend(struct early_suspend *h)
{
        printk("%s\n", __func__);
}

static void esp_late_resume(struct early_suspend*h)
{
        printk("%s\n", __func__);
}

static struct early_suspend esp_early_suspend_ctrl =  {
        .suspend = esp_early_suspend,
        .resume = esp_late_resume,
        .level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 20,
};
#endif /* EARLYSUSPEND */

void esp_register_early_suspend(void)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
        register_early_suspend(&esp_early_suspend_ctrl);
#endif
}

void esp_unregister_early_suspend(void)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
        unregister_early_suspend(&esp_early_suspend_ctrl);
#endif  
}

#ifdef CONFIG_HAS_WAKELOCK
static struct wake_lock esp_wake_lock_;
#endif /* WAKELOCK */

void esp_wakelock_init(void)
{
#ifdef CONFIG_HAS_WAKELOCK
        wake_lock_init(&esp_wake_lock_, WAKE_LOCK_SUSPEND, "eagle");
#endif
}

void esp_wakelock_destroy(void)
{
#ifdef CONFIG_HAS_WAKELOCK
        wake_lock_destroy(&esp_wake_lock_);
#endif
}

void esp_wake_lock(void)
{
#ifdef CONFIG_HAS_WAKELOCK
        wake_lock(&esp_wake_lock_);
#endif
}

void esp_wake_unlock(void)
{
#ifdef CONFIG_HAS_WAKELOCK
        wake_unlock(&esp_wake_lock_);
#endif
}
