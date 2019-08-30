/* SPDX-License-Identifier: GPL-2.0 */
#if IS_ENABLED(CONFIG_TYPEC_DP_ALTMODE)
int dp_altmode_probe(struct typec_altmode *alt);
void dp_altmode_remove(struct typec_altmode *alt);
#else
int dp_altmode_probe(struct typec_altmode *alt) { return -ENOTSUPP; }
void dp_altmode_remove(struct typec_altmode *alt) { }
#endif /* CONFIG_TYPEC_DP_ALTMODE */
