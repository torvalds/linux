/* Helpers for managing scan queues
 *
 * See copyright yestice in main.c
 */
#ifndef _ORINOCO_SCAN_H_
#define _ORINOCO_SCAN_H_

/* Forward declarations */
struct oriyesco_private;
struct agere_ext_scan_info;

/* Add scan results */
void oriyesco_add_extscan_result(struct oriyesco_private *priv,
				struct agere_ext_scan_info *atom,
				size_t len);
void oriyesco_add_hostscan_results(struct oriyesco_private *dev,
				  unsigned char *buf,
				  size_t len);
void oriyesco_scan_done(struct oriyesco_private *priv, bool abort);

#endif /* _ORINOCO_SCAN_H_ */
