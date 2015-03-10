/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef _ESP_WL_H_
#define _ESP_WL_H_

//#define MAX_PROBED_SSID_INDEX 9


enum {
        CONF_HW_BIT_RATE_1MBPS   = BIT(0),
        CONF_HW_BIT_RATE_2MBPS   = BIT(1),
        CONF_HW_BIT_RATE_5_5MBPS = BIT(2),
        CONF_HW_BIT_RATE_11MBPS  = BIT(3),
        CONF_HW_BIT_RATE_6MBPS   = BIT(4),
        CONF_HW_BIT_RATE_9MBPS   = BIT(5),
        CONF_HW_BIT_RATE_12MBPS  = BIT(6),
        CONF_HW_BIT_RATE_18MBPS  = BIT(7),
        CONF_HW_BIT_RATE_22MBPS  = BIT(8),
        CONF_HW_BIT_RATE_24MBPS  = BIT(9),
        CONF_HW_BIT_RATE_36MBPS  = BIT(10),
        CONF_HW_BIT_RATE_48MBPS  = BIT(11),
        CONF_HW_BIT_RATE_54MBPS  = BIT(12),
	CONF_HW_BIT_RATE_11B_MASK = (CONF_HW_BIT_RATE_1MBPS | CONF_HW_BIT_RATE_2MBPS | CONF_HW_BIT_RATE_5_5MBPS | CONF_HW_BIT_RATE_11MBPS),
};

#if 0
enum {
        CONF_HW_RATE_INDEX_1MBPS   = 0,
        CONF_HW_RATE_INDEX_2MBPS   = 1,
        CONF_HW_RATE_INDEX_5_5MBPS = 2,
        CONF_HW_RATE_INDEX_6MBPS   = 3,
        CONF_HW_RATE_INDEX_9MBPS   = 4,
        CONF_HW_RATE_INDEX_11MBPS  = 5,
        CONF_HW_RATE_INDEX_12MBPS  = 6,
        CONF_HW_RATE_INDEX_18MBPS  = 7,
        CONF_HW_RATE_INDEX_22MBPS  = 8,
        CONF_HW_RATE_INDEX_24MBPS  = 9,
        CONF_HW_RATE_INDEX_36MBPS  = 10,
        CONF_HW_RATE_INDEX_48MBPS  = 11,
        CONF_HW_RATE_INDEX_54MBPS  = 12,
        CONF_HW_RATE_INDEX_MAX,
};

enum {
        CONF_HW_RXTX_RATE_54 = 0,
        CONF_HW_RXTX_RATE_48,
        CONF_HW_RXTX_RATE_36,
        CONF_HW_RXTX_RATE_24,
        CONF_HW_RXTX_RATE_22,
        CONF_HW_RXTX_RATE_18,
        CONF_HW_RXTX_RATE_12,
        CONF_HW_RXTX_RATE_11,
        CONF_HW_RXTX_RATE_9,
        CONF_HW_RXTX_RATE_6,
        CONF_HW_RXTX_RATE_5_5,
        CONF_HW_RXTX_RATE_2,
        CONF_HW_RXTX_RATE_1,
        CONF_HW_RXTX_RATE_MAX,
        CONF_HW_RXTX_RATE_UNSUPPORTED = 0xff
};
#endif

#endif /* _ESP_WL_H_ */
