/* If you are going to abuse the preprocessor, why not ABUSE the preprocessor?
   I stuck this header in a separate file so I don't have to look at it */

// FIXME Need locking or atomic ops

#define show_set_mbit(dname,value,bit)																			\
static ssize_t show_##value(struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct usb_##dname *t = usb_get_intfdata(intf);			\
	int temp = (1 && (t->value & (1 << bit)));					\
	return sprintf(buf, "%d\n", temp);			\
}									\
static ssize_t set_##value(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)	\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct usb_##dname *t = usb_get_intfdata(intf);			\
	int temp = simple_strtoul(buf, NULL, 10);			\
	if(temp > 0) { long b = 1 << bit; t->value |= b; } \
	else { long b = ~(1 << bit); t->value &= b ;				 \
	return count;							\
}									\
static DEVICE_ATTR(value, S_IWUGO | S_IRUGO, show_##value, set_##value);

#define show_set_ebit(dname,enumname,value,bit)																			\
static ssize_t show_##bit(struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct usb_##dname *t = usb_get_intfdata(intf);			\
  enum enumname l = bit; \
  int temp = t->value & (1 << l);						\
	return sprintf(buf, "%d\n", temp);			\
}									\
static ssize_t set_##bit(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)	\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct usb_##dname *t = usb_get_intfdata(intf);			\
	int temp = simple_strtoul(buf, NULL, 10);			\
  enum enumname l = bit;\
	long b  = 1 << l; \
	if(temp > 0) { t->value |= b; }	\
	else { t->value &= ~b ;				\
	return count;							\
}									\
static DEVICE_ATTR(value, S_IWUGO | S_IRUGO, show_##value, set_##value);

// FIXME FOR CORRECTLY SETTING HEX from a string
#define show_set_mcmd(dname,value)																			\
static ssize_t show_##value(struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct usb_##dname *t = usb_get_intfdata(intf);			\
  int count = 0;\
	int i; \
	for (i = 0,i<sizeof(dname); i++) count += snprintf(buf, "%02x",t->dname[i]); \
  return(count);\
}									\
static ssize_t set_##value(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)	\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct usb_##dname *t = usb_get_intfdata(intf);			\
	int temp = simple_strtoul(buf, NULL, 10);			\
	t->value = temp;						\
	return count;							\
}									\
static DEVICE_ATTR(value, S_IWUGO | S_IRUGO, show_##value, set_##value);

#define show_set_mint(dname,value)				\
static ssize_t show_##value(struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct usb_##dname *t = usb_get_intfdata(intf);			\
	return sprintf(buf, "%d\n", t->value);			\
}									\
static ssize_t set_##value(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)	\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct usb_##dname *t = usb_get_intfdata(intf);			\
	int temp = simple_strtoul(buf, NULL, 10);			\
	t->value = temp;						\
	return count;							\
}									\
static DEVICE_ATTR(value, S_IWUGO | S_IRUGO, show_##value, set_##value);

#define show_set_mchar(dname,value)																				\
static ssize_t show_##value(struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct usb_##dname *t = usb_get_intfdata(intf);			\
	return sprintf(buf, "%c\n", t->value);			\
}									\
static ssize_t set_##value(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)	\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct usb_##dname *t = usb_get_intfdata(intf);			\
	int temp = simple_strtoul(buf, NULL, 10);			\
	t->value = temp;						\
	return count;							\
}									\
static DEVICE_ATTR(value, S_IWUGO | S_IRUGO, show_##value, set_##value);
