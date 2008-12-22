/*
 * WiMedia Logical Link Control Protocol (WLP)
 * sysfs functions
 *
 * Copyright (C) 2007 Intel Corporation
 * Reinette Chatre <reinette.chatre@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * FIXME: Docs
 *
 */
#include <linux/wlp.h>

#include "wlp-internal.h"

static
size_t wlp_wss_wssid_e_print(char *buf, size_t bufsize,
			     struct wlp_wssid_e *wssid_e)
{
	size_t used = 0;
	used += scnprintf(buf, bufsize, " WSS: ");
	used += wlp_wss_uuid_print(buf + used, bufsize - used,
				   &wssid_e->wssid);

	if (wssid_e->info != NULL) {
		used += scnprintf(buf + used, bufsize - used, " ");
		used += uwb_mac_addr_print(buf + used, bufsize - used,
					   &wssid_e->info->bcast);
		used += scnprintf(buf + used, bufsize - used, " %u %u %s\n",
				  wssid_e->info->accept_enroll,
				  wssid_e->info->sec_status,
				  wssid_e->info->name);
	}
	return used;
}

/**
 * Print out information learned from neighbor discovery
 *
 * Some fields being printed may not be included in the device discovery
 * information (it is not mandatory). We are thus careful how the
 * information is printed to ensure it is clear to the user what field is
 * being referenced.
 * The information being printed is for one time use - temporary storage is
 * cleaned after it is printed.
 *
 * Ideally sysfs output should be on one line. The information printed here
 * contain a few strings so it will be hard to parse if they are all
 * printed on the same line - without agreeing on a standard field
 * separator.
 */
static
ssize_t wlp_wss_neighborhood_print_remove(struct wlp *wlp, char *buf,
				   size_t bufsize)
{
	size_t used = 0;
	struct wlp_neighbor_e *neighb;
	struct wlp_wssid_e *wssid_e;

	mutex_lock(&wlp->nbmutex);
	used = scnprintf(buf, bufsize, "#Neighbor information\n"
			 "#uuid dev_addr\n"
			 "# Device Name:\n# Model Name:\n# Manufacturer:\n"
			 "# Model Nr:\n# Serial:\n"
			 "# Pri Dev type: CategoryID OUI OUISubdiv "
			 "SubcategoryID\n"
			 "# WSS: WSSID WSS_name accept_enroll sec_status "
			 "bcast\n"
			 "# WSS: WSSID WSS_name accept_enroll sec_status "
			 "bcast\n\n");
	list_for_each_entry(neighb, &wlp->neighbors, node) {
		if (bufsize - used <= 0)
			goto out;
		used += wlp_wss_uuid_print(buf + used, bufsize - used,
					   &neighb->uuid);
		buf[used++] = ' ';
		used += uwb_dev_addr_print(buf + used, bufsize - used,
					   &neighb->uwb_dev->dev_addr);
		if (neighb->info != NULL)
			used += scnprintf(buf + used, bufsize - used,
					  "\n Device Name: %s\n"
					  " Model Name: %s\n"
					  " Manufacturer:%s \n"
					  " Model Nr: %s\n"
					  " Serial: %s\n"
					  " Pri Dev type: "
					  "%u %02x:%02x:%02x %u %u\n",
					  neighb->info->name,
					  neighb->info->model_name,
					  neighb->info->manufacturer,
					  neighb->info->model_nr,
					  neighb->info->serial,
					  neighb->info->prim_dev_type.category,
					  neighb->info->prim_dev_type.OUI[0],
					  neighb->info->prim_dev_type.OUI[1],
					  neighb->info->prim_dev_type.OUI[2],
					  neighb->info->prim_dev_type.OUIsubdiv,
					  neighb->info->prim_dev_type.subID);
		list_for_each_entry(wssid_e, &neighb->wssid, node) {
			used += wlp_wss_wssid_e_print(buf + used,
						      bufsize - used,
						      wssid_e);
		}
		buf[used++] = '\n';
		wlp_remove_neighbor_tmp_info(neighb);
	}


out:
	mutex_unlock(&wlp->nbmutex);
	return used;
}


/**
 * Show properties of all WSS in neighborhood.
 *
 * Will trigger a complete discovery of WSS activated by this device and
 * its neighbors.
 */
ssize_t wlp_neighborhood_show(struct wlp *wlp, char *buf)
{
	wlp_discover(wlp);
	return wlp_wss_neighborhood_print_remove(wlp, buf, PAGE_SIZE);
}
EXPORT_SYMBOL_GPL(wlp_neighborhood_show);

static
ssize_t __wlp_wss_properties_show(struct wlp_wss *wss, char *buf,
				  size_t bufsize)
{
	ssize_t result;

	result = wlp_wss_uuid_print(buf, bufsize, &wss->wssid);
	result += scnprintf(buf + result, bufsize - result, " ");
	result += uwb_mac_addr_print(buf + result, bufsize - result,
				     &wss->bcast);
	result += scnprintf(buf + result, bufsize - result,
			    " 0x%02x %u ", wss->hash, wss->secure_status);
	result += wlp_wss_key_print(buf + result, bufsize - result,
				    wss->master_key);
	result += scnprintf(buf + result, bufsize - result, " 0x%02x ",
			    wss->tag);
	result += uwb_mac_addr_print(buf + result, bufsize - result,
				     &wss->virtual_addr);
	result += scnprintf(buf + result, bufsize - result, " %s", wss->name);
	result += scnprintf(buf + result, bufsize - result,
			    "\n\n#WSSID\n#WSS broadcast address\n"
			    "#WSS hash\n#WSS secure status\n"
			    "#WSS master key\n#WSS local tag\n"
			    "#WSS local virtual EUI-48\n#WSS name\n");
	return result;
}

/**
 * Show which WSS is activated.
 */
ssize_t wlp_wss_activate_show(struct wlp_wss *wss, char *buf)
{
	int result = 0;

	if (mutex_lock_interruptible(&wss->mutex))
		goto out;
	if (wss->state >= WLP_WSS_STATE_ACTIVE)
		result = __wlp_wss_properties_show(wss, buf, PAGE_SIZE);
	else
		result = scnprintf(buf, PAGE_SIZE, "No local WSS active.\n");
	result += scnprintf(buf + result, PAGE_SIZE - result,
			"\n\n"
			"# echo WSSID SECURE_STATUS ACCEPT_ENROLLMENT "
			"NAME #create new WSS\n"
			"# echo WSSID [DEV ADDR] #enroll in and activate "
			"existing WSS, can request registrar\n"
			"#\n"
			"# WSSID is a 16 byte hex array. Eg. 12 A3 3B ... \n"
			"# SECURE_STATUS 0 - unsecure, 1 - secure (default)\n"
			"# ACCEPT_ENROLLMENT 0 - no, 1 - yes (default)\n"
			"# NAME is the text string identifying the WSS\n"
			"# DEV ADDR is the device address of neighbor "
			"that should be registrar. Eg. 32:AB\n");

	mutex_unlock(&wss->mutex);
out:
	return result;

}
EXPORT_SYMBOL_GPL(wlp_wss_activate_show);

/**
 * Create/activate a new WSS or enroll/activate in neighboring WSS
 *
 * The user can provide the WSSID of a WSS in which it wants to enroll.
 * Only the WSSID is necessary if the WSS have been discovered before. If
 * the WSS has not been discovered before, or the user wants to use a
 * particular neighbor as its registrar, then the user can also provide a
 * device address or the neighbor that will be used as registrar.
 *
 * A new WSS is created when the user provides a WSSID, secure status, and
 * WSS name.
 */
ssize_t wlp_wss_activate_store(struct wlp_wss *wss,
			       const char *buf, size_t size)
{
	ssize_t result = -EINVAL;
	struct wlp_uuid wssid;
	struct uwb_dev_addr dev;
	struct uwb_dev_addr bcast = {.data = {0xff, 0xff} };
	char name[65];
	unsigned sec_status, accept;
	memset(name, 0, sizeof(name));
	result = sscanf(buf, "%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx:%02hhx",
			&wssid.data[0] , &wssid.data[1],
			&wssid.data[2] , &wssid.data[3],
			&wssid.data[4] , &wssid.data[5],
			&wssid.data[6] , &wssid.data[7],
			&wssid.data[8] , &wssid.data[9],
			&wssid.data[10], &wssid.data[11],
			&wssid.data[12], &wssid.data[13],
			&wssid.data[14], &wssid.data[15],
			&dev.data[1], &dev.data[0]);
	if (result == 16 || result == 17) {
		result = sscanf(buf, "%02hhx %02hhx %02hhx %02hhx "
				"%02hhx %02hhx %02hhx %02hhx "
				"%02hhx %02hhx %02hhx %02hhx "
				"%02hhx %02hhx %02hhx %02hhx "
				"%u %u %64c",
				&wssid.data[0] , &wssid.data[1],
				&wssid.data[2] , &wssid.data[3],
				&wssid.data[4] , &wssid.data[5],
				&wssid.data[6] , &wssid.data[7],
				&wssid.data[8] , &wssid.data[9],
				&wssid.data[10], &wssid.data[11],
				&wssid.data[12], &wssid.data[13],
				&wssid.data[14], &wssid.data[15],
				&sec_status, &accept, name);
		if (result == 16)
			result = wlp_wss_enroll_activate(wss, &wssid, &bcast);
		else if (result == 19) {
			sec_status = sec_status == 0 ? 0 : 1;
			accept = accept == 0 ? 0 : 1;
			/* We read name using %c, so the newline needs to be
			 * removed */
			if (strlen(name) != sizeof(name) - 1)
				name[strlen(name) - 1] = '\0';
			result = wlp_wss_create_activate(wss, &wssid, name,
							 sec_status, accept);
		} else
			result = -EINVAL;
	} else if (result == 18)
		result = wlp_wss_enroll_activate(wss, &wssid, &dev);
	else
		result = -EINVAL;
	return result < 0 ? result : size;
}
EXPORT_SYMBOL_GPL(wlp_wss_activate_store);

/**
 * Show the UUID of this host
 */
ssize_t wlp_uuid_show(struct wlp *wlp, char *buf)
{
	ssize_t result = 0;

	mutex_lock(&wlp->mutex);
	result = wlp_wss_uuid_print(buf, PAGE_SIZE, &wlp->uuid);
	buf[result++] = '\n';
	mutex_unlock(&wlp->mutex);
	return result;
}
EXPORT_SYMBOL_GPL(wlp_uuid_show);

/**
 * Store a new UUID for this host
 *
 * According to the spec this should be encoded as an octet string in the
 * order the octets are shown in string representation in RFC 4122 (WLP
 * 0.99 [Table 6])
 *
 * We do not check value provided by user.
 */
ssize_t wlp_uuid_store(struct wlp *wlp, const char *buf, size_t size)
{
	ssize_t result;
	struct wlp_uuid uuid;

	mutex_lock(&wlp->mutex);
	result = sscanf(buf, "%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx ",
			&uuid.data[0] , &uuid.data[1],
			&uuid.data[2] , &uuid.data[3],
			&uuid.data[4] , &uuid.data[5],
			&uuid.data[6] , &uuid.data[7],
			&uuid.data[8] , &uuid.data[9],
			&uuid.data[10], &uuid.data[11],
			&uuid.data[12], &uuid.data[13],
			&uuid.data[14], &uuid.data[15]);
	if (result != 16) {
		result = -EINVAL;
		goto error;
	}
	wlp->uuid = uuid;
error:
	mutex_unlock(&wlp->mutex);
	return result < 0 ? result : size;
}
EXPORT_SYMBOL_GPL(wlp_uuid_store);

/**
 * Show contents of members of device information structure
 */
#define wlp_dev_info_show(type)						\
ssize_t wlp_dev_##type##_show(struct wlp *wlp, char *buf)		\
{									\
	ssize_t result = 0;						\
	mutex_lock(&wlp->mutex);					\
	if (wlp->dev_info == NULL) {					\
		result = __wlp_setup_device_info(wlp);			\
		if (result < 0)						\
			goto out;					\
	}								\
	result = scnprintf(buf, PAGE_SIZE, "%s\n", wlp->dev_info->type);\
out:									\
	mutex_unlock(&wlp->mutex);					\
	return result;							\
}									\
EXPORT_SYMBOL_GPL(wlp_dev_##type##_show);

wlp_dev_info_show(name)
wlp_dev_info_show(model_name)
wlp_dev_info_show(model_nr)
wlp_dev_info_show(manufacturer)
wlp_dev_info_show(serial)

/**
 * Store contents of members of device information structure
 */
#define wlp_dev_info_store(type, len)					\
ssize_t wlp_dev_##type##_store(struct wlp *wlp, const char *buf, size_t size)\
{									\
	ssize_t result;							\
	char format[10];						\
	mutex_lock(&wlp->mutex);					\
	if (wlp->dev_info == NULL) {					\
		result = __wlp_alloc_device_info(wlp);			\
		if (result < 0)						\
			goto out;					\
	}								\
	memset(wlp->dev_info->type, 0, sizeof(wlp->dev_info->type));	\
	sprintf(format, "%%%uc", len);					\
	result = sscanf(buf, format, wlp->dev_info->type);		\
out:									\
	mutex_unlock(&wlp->mutex);					\
	return result < 0 ? result : size;				\
}									\
EXPORT_SYMBOL_GPL(wlp_dev_##type##_store);

wlp_dev_info_store(name, 32)
wlp_dev_info_store(manufacturer, 64)
wlp_dev_info_store(model_name, 32)
wlp_dev_info_store(model_nr, 32)
wlp_dev_info_store(serial, 32)

static
const char *__wlp_dev_category[] = {
	[WLP_DEV_CAT_COMPUTER] = "Computer",
	[WLP_DEV_CAT_INPUT] = "Input device",
	[WLP_DEV_CAT_PRINT_SCAN_FAX_COPIER] = "Printer, scanner, FAX, or "
					      "Copier",
	[WLP_DEV_CAT_CAMERA] = "Camera",
	[WLP_DEV_CAT_STORAGE] = "Storage Network",
	[WLP_DEV_CAT_INFRASTRUCTURE] = "Infrastructure",
	[WLP_DEV_CAT_DISPLAY] = "Display",
	[WLP_DEV_CAT_MULTIM] = "Multimedia device",
	[WLP_DEV_CAT_GAMING] = "Gaming device",
	[WLP_DEV_CAT_TELEPHONE] = "Telephone",
	[WLP_DEV_CAT_OTHER] = "Other",
};

static
const char *wlp_dev_category_str(unsigned cat)
{
	if ((cat >= WLP_DEV_CAT_COMPUTER && cat <= WLP_DEV_CAT_TELEPHONE)
	    || cat == WLP_DEV_CAT_OTHER)
		return __wlp_dev_category[cat];
	return "unknown category";
}

ssize_t wlp_dev_prim_category_show(struct wlp *wlp, char *buf)
{
	ssize_t result = 0;
	mutex_lock(&wlp->mutex);
	if (wlp->dev_info == NULL) {
		result = __wlp_setup_device_info(wlp);
		if (result < 0)
			goto out;
	}
	result = scnprintf(buf, PAGE_SIZE, "%s\n",
		  wlp_dev_category_str(wlp->dev_info->prim_dev_type.category));
out:
	mutex_unlock(&wlp->mutex);
	return result;
}
EXPORT_SYMBOL_GPL(wlp_dev_prim_category_show);

ssize_t wlp_dev_prim_category_store(struct wlp *wlp, const char *buf,
				    size_t size)
{
	ssize_t result;
	u16 cat;
	mutex_lock(&wlp->mutex);
	if (wlp->dev_info == NULL) {
		result = __wlp_alloc_device_info(wlp);
		if (result < 0)
			goto out;
	}
	result = sscanf(buf, "%hu", &cat);
	if ((cat >= WLP_DEV_CAT_COMPUTER && cat <= WLP_DEV_CAT_TELEPHONE)
	    || cat == WLP_DEV_CAT_OTHER)
		wlp->dev_info->prim_dev_type.category = cat;
	else
		result = -EINVAL;
out:
	mutex_unlock(&wlp->mutex);
	return result < 0 ? result : size;
}
EXPORT_SYMBOL_GPL(wlp_dev_prim_category_store);

ssize_t wlp_dev_prim_OUI_show(struct wlp *wlp, char *buf)
{
	ssize_t result = 0;
	mutex_lock(&wlp->mutex);
	if (wlp->dev_info == NULL) {
		result = __wlp_setup_device_info(wlp);
		if (result < 0)
			goto out;
	}
	result = scnprintf(buf, PAGE_SIZE, "%02x:%02x:%02x\n",
			   wlp->dev_info->prim_dev_type.OUI[0],
			   wlp->dev_info->prim_dev_type.OUI[1],
			   wlp->dev_info->prim_dev_type.OUI[2]);
out:
	mutex_unlock(&wlp->mutex);
	return result;
}
EXPORT_SYMBOL_GPL(wlp_dev_prim_OUI_show);

ssize_t wlp_dev_prim_OUI_store(struct wlp *wlp, const char *buf, size_t size)
{
	ssize_t result;
	u8 OUI[3];
	mutex_lock(&wlp->mutex);
	if (wlp->dev_info == NULL) {
		result = __wlp_alloc_device_info(wlp);
		if (result < 0)
			goto out;
	}
	result = sscanf(buf, "%hhx:%hhx:%hhx",
			&OUI[0], &OUI[1], &OUI[2]);
	if (result != 3) {
		result = -EINVAL;
		goto out;
	} else
		memcpy(wlp->dev_info->prim_dev_type.OUI, OUI, sizeof(OUI));
out:
	mutex_unlock(&wlp->mutex);
	return result < 0 ? result : size;
}
EXPORT_SYMBOL_GPL(wlp_dev_prim_OUI_store);


ssize_t wlp_dev_prim_OUI_sub_show(struct wlp *wlp, char *buf)
{
	ssize_t result = 0;
	mutex_lock(&wlp->mutex);
	if (wlp->dev_info == NULL) {
		result = __wlp_setup_device_info(wlp);
		if (result < 0)
			goto out;
	}
	result = scnprintf(buf, PAGE_SIZE, "%u\n",
			   wlp->dev_info->prim_dev_type.OUIsubdiv);
out:
	mutex_unlock(&wlp->mutex);
	return result;
}
EXPORT_SYMBOL_GPL(wlp_dev_prim_OUI_sub_show);

ssize_t wlp_dev_prim_OUI_sub_store(struct wlp *wlp, const char *buf,
				   size_t size)
{
	ssize_t result;
	unsigned sub;
	u8 max_sub = ~0;
	mutex_lock(&wlp->mutex);
	if (wlp->dev_info == NULL) {
		result = __wlp_alloc_device_info(wlp);
		if (result < 0)
			goto out;
	}
	result = sscanf(buf, "%u", &sub);
	if (sub <= max_sub)
		wlp->dev_info->prim_dev_type.OUIsubdiv = sub;
	else
		result = -EINVAL;
out:
	mutex_unlock(&wlp->mutex);
	return result < 0 ? result : size;
}
EXPORT_SYMBOL_GPL(wlp_dev_prim_OUI_sub_store);

ssize_t wlp_dev_prim_subcat_show(struct wlp *wlp, char *buf)
{
	ssize_t result = 0;
	mutex_lock(&wlp->mutex);
	if (wlp->dev_info == NULL) {
		result = __wlp_setup_device_info(wlp);
		if (result < 0)
			goto out;
	}
	result = scnprintf(buf, PAGE_SIZE, "%u\n",
			   wlp->dev_info->prim_dev_type.subID);
out:
	mutex_unlock(&wlp->mutex);
	return result;
}
EXPORT_SYMBOL_GPL(wlp_dev_prim_subcat_show);

ssize_t wlp_dev_prim_subcat_store(struct wlp *wlp, const char *buf,
				  size_t size)
{
	ssize_t result;
	unsigned sub;
	__le16 max_sub = ~0;
	mutex_lock(&wlp->mutex);
	if (wlp->dev_info == NULL) {
		result = __wlp_alloc_device_info(wlp);
		if (result < 0)
			goto out;
	}
	result = sscanf(buf, "%u", &sub);
	if (sub <= max_sub)
		wlp->dev_info->prim_dev_type.subID = sub;
	else
		result = -EINVAL;
out:
	mutex_unlock(&wlp->mutex);
	return result < 0 ? result : size;
}
EXPORT_SYMBOL_GPL(wlp_dev_prim_subcat_store);

/**
 * Subsystem implementation for interaction with individual WSS via sysfs
 *
 * Followed instructions for subsystem in Documentation/filesystems/sysfs.txt
 */

#define kobj_to_wlp_wss(obj) container_of(obj, struct wlp_wss, kobj)
#define attr_to_wlp_wss_attr(_attr) \
	container_of(_attr, struct wlp_wss_attribute, attr)

/**
 * Sysfs subsystem: forward read calls
 *
 * Sysfs operation for forwarding read call to the show method of the
 * attribute owner
 */
static
ssize_t wlp_wss_attr_show(struct kobject *kobj, struct attribute *attr,
			  char *buf)
{
	struct wlp_wss_attribute *wss_attr = attr_to_wlp_wss_attr(attr);
	struct wlp_wss *wss = kobj_to_wlp_wss(kobj);
	ssize_t ret = -EIO;

	if (wss_attr->show)
		ret = wss_attr->show(wss, buf);
	return ret;
}
/**
 * Sysfs subsystem: forward write calls
 *
 * Sysfs operation for forwarding write call to the store method of the
 * attribute owner
 */
static
ssize_t wlp_wss_attr_store(struct kobject *kobj, struct attribute *attr,
			   const char *buf, size_t count)
{
	struct wlp_wss_attribute *wss_attr = attr_to_wlp_wss_attr(attr);
	struct wlp_wss *wss = kobj_to_wlp_wss(kobj);
	ssize_t ret = -EIO;

	if (wss_attr->store)
		ret = wss_attr->store(wss, buf, count);
	return ret;
}

static
struct sysfs_ops wss_sysfs_ops = {
	.show	= wlp_wss_attr_show,
	.store	= wlp_wss_attr_store,
};

struct kobj_type wss_ktype = {
	.release	= wlp_wss_release,
	.sysfs_ops	= &wss_sysfs_ops,
};


/**
 * Sysfs files for individual WSS
 */

/**
 * Print static properties of this WSS
 *
 * The name of a WSS may not be null teminated. It's max size is 64 bytes
 * so we copy it to a larger array just to make sure we print sane data.
 */
static ssize_t wlp_wss_properties_show(struct wlp_wss *wss, char *buf)
{
	int result = 0;

	if (mutex_lock_interruptible(&wss->mutex))
		goto out;
	result = __wlp_wss_properties_show(wss, buf, PAGE_SIZE);
	mutex_unlock(&wss->mutex);
out:
	return result;
}
WSS_ATTR(properties, S_IRUGO, wlp_wss_properties_show, NULL);

/**
 * Print all connected members of this WSS
 * The EDA cache contains all members of WSS neighborhood.
 */
static ssize_t wlp_wss_members_show(struct wlp_wss *wss, char *buf)
{
	struct wlp *wlp = container_of(wss, struct wlp, wss);
	return wlp_eda_show(wlp, buf);
}
WSS_ATTR(members, S_IRUGO, wlp_wss_members_show, NULL);

static
const char *__wlp_strstate[] = {
	"none",
	"partially enrolled",
	"enrolled",
	"active",
	"connected",
};

static const char *wlp_wss_strstate(unsigned state)
{
	if (state >= ARRAY_SIZE(__wlp_strstate))
		return "unknown state";
	return __wlp_strstate[state];
}

/*
 * Print current state of this WSS
 */
static ssize_t wlp_wss_state_show(struct wlp_wss *wss, char *buf)
{
	int result = 0;

	if (mutex_lock_interruptible(&wss->mutex))
		goto out;
	result = scnprintf(buf, PAGE_SIZE, "%s\n",
			   wlp_wss_strstate(wss->state));
	mutex_unlock(&wss->mutex);
out:
	return result;
}
WSS_ATTR(state, S_IRUGO, wlp_wss_state_show, NULL);


static
struct attribute *wss_attrs[] = {
	&wss_attr_properties.attr,
	&wss_attr_members.attr,
	&wss_attr_state.attr,
	NULL,
};

struct attribute_group wss_attr_group = {
	.name = NULL,	/* we want them in the same directory */
	.attrs = wss_attrs,
};
