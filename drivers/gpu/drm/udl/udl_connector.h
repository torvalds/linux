#ifndef __UDL_CONNECTOR_H__
#define __UDL_CONNECTOR_H__

#include <linux/container_of.h>

#include <drm/drm_connector.h>

struct edid;

struct udl_connector {
	struct drm_connector connector;
	/* last udl_detect edid */
	struct edid *edid;
};

static inline struct udl_connector *to_udl_connector(struct drm_connector *connector)
{
	return container_of(connector, struct udl_connector, connector);
}

#endif //__UDL_CONNECTOR_H__
