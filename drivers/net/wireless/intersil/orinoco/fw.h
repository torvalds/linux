/* Firmware file reading and download helpers
 *
 * See copyright notice in main.c
 */
#ifndef _ORINOCO_FW_H_
#define _ORINOCO_FW_H_

/* Forward declations */
struct orinoco_private;

int orinoco_download(struct orinoco_private *priv);

#if defined(CONFIG_HERMES_CACHE_FW_ON_INIT) || defined(CONFIG_PM_SLEEP)
void orinoco_cache_fw(struct orinoco_private *priv, int ap);
void orinoco_uncache_fw(struct orinoco_private *priv);
#else
#define orinoco_cache_fw(priv, ap) do { } while (0)
#define orinoco_uncache_fw(priv) do { } while (0)
#endif

#endif /* _ORINOCO_FW_H_ */
