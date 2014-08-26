#ifndef _ESP_LOG_H_
#define _ESP_LOG_H_

#ifdef ESP_DEBUG

#define loge(fmt, args...) do { \
            printk(fmt, ##args); \
        } while (0)

#define logi(fmt, args...) do { \
            printk(fmt, ##args); \
        } while (0)

#define logd(fmt, args...) do { \
            printk(fmt, ##args); \
        } while (0)
#else

#define loge(fmt, args...) do { \
            printk(fmt, ##args); \
        } while (0)

#define logi(fmt, args...) do { \
            printk(fmt, ##args); \
        } while (0)

#define logd(fmt, args...) do { \
        } while (0)

#endif /* ESP_DEBUG */
   
#endif /* _ESP_LOG_H_*/
