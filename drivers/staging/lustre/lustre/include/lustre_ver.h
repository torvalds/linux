#ifndef _LUSTRE_VER_H_
#define _LUSTRE_VER_H_

#define LUSTRE_MAJOR 2
#define LUSTRE_MINOR 4
#define LUSTRE_PATCH 60
#define LUSTRE_FIX 0
#define LUSTRE_VERSION_STRING "2.4.60"

#define OBD_OCD_VERSION(major, minor, patch, fix)			\
	(((major) << 24) + ((minor) << 16) + ((patch) << 8) + (fix))

#define OBD_OCD_VERSION_MAJOR(version)	((int)((version) >> 24) & 255)
#define OBD_OCD_VERSION_MINOR(version)	((int)((version) >> 16) & 255)
#define OBD_OCD_VERSION_PATCH(version)	((int)((version) >>  8) & 255)
#define OBD_OCD_VERSION_FIX(version)	((int)((version) >>  0) & 255)

#define LUSTRE_VERSION_CODE						\
	OBD_OCD_VERSION(LUSTRE_MAJOR, LUSTRE_MINOR, LUSTRE_PATCH, LUSTRE_FIX)

/*
 * If lustre version of client and servers it connects to differs by more
 * than this amount, client would issue a warning.
 */
#define LUSTRE_VERSION_OFFSET_WARN OBD_OCD_VERSION(0, 4, 0, 0)

#endif
