/* Helpers for managing scan queues
 *
 * See copyright notice in main.c
 */
#ifndef _ORINOCO_SCAN_H_
#define _ORINOCO_SCAN_H_

/* Forward declarations */
struct orinoco_private;
struct agere_ext_scan_info;

/* Setup and free memory for scan results */
int orinoco_bss_data_allocate(struct orinoco_private *priv);
void orinoco_bss_data_free(struct orinoco_private *priv);
void orinoco_bss_data_init(struct orinoco_private *priv);

/* Add scan results */
void orinoco_add_ext_scan_result(struct orinoco_private *priv,
				 struct agere_ext_scan_info *atom);
int orinoco_process_scan_results(struct orinoco_private *dev,
				 unsigned char *buf,
				 int len);

/* Clear scan results */
void orinoco_clear_scan_results(struct orinoco_private *priv,
				unsigned long scan_age);


#endif /* _ORINOCO_SCAN_H_ */
