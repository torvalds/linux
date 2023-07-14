/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __QCA8K_LEDS_H
#define __QCA8K_LEDS_H

/* Leds Support function */
#ifdef CONFIG_NET_DSA_QCA8K_LEDS_SUPPORT
int qca8k_setup_led_ctrl(struct qca8k_priv *priv);
#else
static inline int qca8k_setup_led_ctrl(struct qca8k_priv *priv)
{
	return 0;
}
#endif

#endif /* __QCA8K_LEDS_H */
