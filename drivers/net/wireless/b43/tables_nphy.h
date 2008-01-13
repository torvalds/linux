#ifndef B43_TABLES_NPHY_H_
#define B43_TABLES_NPHY_H_

#include <linux/types.h>


struct b43_wldev;

/* Upload the default register value table.
 * If "ghz5" is true, we upload the 5Ghz table. Otherwise the 2.4Ghz
 * table is uploaded. If "ignore_uploadflag" is true, we upload any value
 * and ignore the "UPLOAD" flag. */
void b2055_upload_inittab(struct b43_wldev *dev,
			  bool ghz5, bool ignore_uploadflag);


#endif /* B43_TABLES_NPHY_H_ */
