#include <linux/export.h>
#include <linux/hdmi-notifier.h>
#include <linux/notifier.h>
#include <linux/string.h>

static BLOCKING_NOTIFIER_HEAD(hdmi_notifier);

int hdmi_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&hdmi_notifier, nb);
}
EXPORT_SYMBOL_GPL(hdmi_register_notifier);

int hdmi_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&hdmi_notifier, nb);
}
EXPORT_SYMBOL_GPL(hdmi_unregister_notifier);

void hdmi_event_connect(struct device *dev)
{
	struct hdmi_event_base base;

	base.source = dev;

	blocking_notifier_call_chain(&hdmi_notifier, HDMI_CONNECTED, &base);
}
EXPORT_SYMBOL_GPL(hdmi_event_connect);

void hdmi_event_disconnect(struct device *dev)
{
	struct hdmi_event_base base;

	base.source = dev;

	blocking_notifier_call_chain(&hdmi_notifier, HDMI_DISCONNECTED, &base);
}
EXPORT_SYMBOL_GPL(hdmi_event_disconnect);

void hdmi_event_new_edid(struct device *dev, const void *edid, size_t size)
{
	struct hdmi_event_new_edid new_edid;

	new_edid.base.source = dev;
	new_edid.edid = edid;
	new_edid.size = size;

	blocking_notifier_call_chain(&hdmi_notifier, HDMI_NEW_EDID, &new_edid);
}
EXPORT_SYMBOL_GPL(hdmi_event_new_edid);

void hdmi_event_new_eld(struct device *dev, const void *eld)
{
	struct hdmi_event_new_eld new_eld;

	new_eld.base.source = dev;
	memcpy(new_eld.eld, eld, sizeof(new_eld.eld));

	blocking_notifier_call_chain(&hdmi_notifier, HDMI_NEW_ELD, &new_eld);
}
EXPORT_SYMBOL_GPL(hdmi_event_new_eld);
