/* This was an attempt - ultimately proved pointless - at making a full fledged sysfs interface to the alphatrack */
/* won't even compile at present */

char *alphatrack_sys_margs;
spinlock_t alphatrack_sys_margs_lock;

struct alphatrack_attr {
        struct attribute attr;
        ssize_t (*show)(struct device *, char *);
        ssize_t (*store)(struct device *, const char *, size_t);
};

#define ALPHATRACK_ATTR(name, mode, show, store) \
static struct alphatrack_attr alphatrack_attr_##name = __ATTR(name, mode, show, store)

/* now a great deal of callback code generation */

// FOREACH_LIGHT(show_set_light)
// FOREACH_BUTTON(show_set_button)

show_set_light(LIGHT_RECORD); show_set_light(LIGHT_EQ); show_set_light(LIGHT_OUT);
show_set_light(LIGHT_F2); show_set_light(LIGHT_SEND);   show_set_light(LIGHT_IN);
show_set_light(LIGHT_F1); show_set_light(LIGHT_PAN);    show_set_light(LIGHT_UNDEF1);
show_set_light(LIGHT_UNDEF2); show_set_light(LIGHT_SHIFT); show_set_light(LIGHT_TRACKMUTE);
show_set_light(LIGHT_TRACKSOLO); show_set_light(LIGHT_TRACKREC); show_set_light(LIGHT_READ);
show_set_light(LIGHT_WRITE); show_set_light(LIGHT_ANYSOLO); show_set_light(LIGHT_AUTO);
show_set_light(LIGHT_F4); show_set_light(LIGHT_RECORD); show_set_light(LIGHT_WINDOW);
show_set_light(LIGHT_PLUGIN); show_set_light(LIGHT_F3); show_set_light(LIGHT_LOOP);

show_set_opt(enable); show_set_opt(offline); show_set_opt(compress_fader); show_set_opt(dump_state);
show_set_int(fader); show_set_int(event);


static ssize_t show_lights(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_alphatrack *t = usb_get_intfdata(intf);
	return sprintf(buf, "%d\n", t->lights);
}

static ssize_t set_lights(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_alphatrack *t = usb_get_intfdata(intf);
	int temp = simple_strtoul(buf, NULL, 10);
	t->lights = temp;
	return count;
}

static DEVICE_ATTR(value, S_IWUGO | S_IRUGO, show_lights, set_lights);


ALPHATRACK_ATTR(LightRecord, 0200, NULL,          LightRecord_store);

static struct attribute *alphatrack_attrs[] = {
        &alphatrack_attr_LightRecord.attr,
        NULL,
};

static ssize_t alphatrack_attr_show(struct kobject *kobj, struct attribute *attr,
                              char *buf)
{
        struct device *sdp = container_of(kobj, struct device, kobj);
        struct alphatrack_attr *a = container_of(attr, struct alphatrack_attr, attr);
        return a->show ? a->show(sdp, buf) : 0;
}

static ssize_t alphatrack_attr_store(struct kobject *kobj, struct attribute *attr,
                               const char *buf, size_t len)
{
        struct device *sdp = container_of(kobj, struct device, kobj);
        struct alphatrack_attr *a = container_of(attr, struct alphatrack_attr, attr);
        return a->store ? a->store(sdp, buf, len) : len;
}

static struct sysfs_ops alphatrack_attr_ops = {
        .show  = alphatrack_attr_show,
        .store = alphatrack_attr_store,
};

static struct kobj_type alphatrack_ktype = {
        .default_attrs = alphatrack_attrs,
        .sysfs_ops     = &alphatrack_attr_ops,
};

static struct kset alphatrack_kset = {
        .subsys = &fs_subsys,
        .kobj   = {.name = "alphatrack"},
        .ktype  = &alphatrack_ktype,
};


static struct attribute *lights_attrs[] = {
        &tune_attr_demote_secs.attr,
        NULL,
};


static struct attribute_group leds_group = {
        .name = "leds",
        .attrs = lights_attrs,
};

static struct attribute_group faders_group = {
        .name = "faders",
        .attrs = faders_attrs,
};

static struct attribute_group lcds_group = {
        .name = "lcds",
        .attrs = lcds_attrs,
};

static struct attribute_group wheels_group = {
        .name = "wheels",
        .attrs = wheels_attrs,
};

static struct attribute_group touchsurfaces_group = {
        .name = "touchsurfaces",
        .attrs = touchsurfaces_attrs,
};

static struct attribute_group buttons_group = {
        .name = "buttons",
        .attrs = buttons_attrs,
};


int alphatrack_sys_fs_add(struct device *sdp)
{
        int error;

        sdp->kobj.kset = &alphatrack_kset;
        sdp->kobj.ktype = &alphatrack_ktype;

//        error = kobject_set_name(&sdp->kobj, "%s", sdp->sd_table_name);
        error = kobject_set_name(&sdp->kobj, "%s", "alphatrack");
        if (error)
                goto fail;

        error = kobject_register(&sdp->kobj);
        if (error)
                goto fail;

        error = sysfs_create_group(&sdp->kobj, &lcds_group);
        if (error)
                goto fail_reg;

        error = sysfs_create_group(&sdp->kobj, &leds_group);
        if (error)
                goto fail_leds;

        error = sysfs_create_group(&sdp->kobj, &wheels_group);
        if (error)
                goto fail_wheels;

      error = sysfs_create_group(&sdp->kobj, &faders_group);
        if (error)
                goto fail_lcds;

        error = sysfs_create_group(&sdp->kobj, &buttons_group);
        if (error)
                goto fail_faders;

        error = sysfs_create_group(&sdp->kobj, &touchsurfaces_group);
        if (error)
                goto fail_buttons;

        return 0;


fail_buttons:
        sysfs_remove_group(&sdp->kobj, &buttons_group);
fail_faders:
        sysfs_remove_group(&sdp->kobj, &faders_group);
fail_wheels:
        sysfs_remove_group(&sdp->kobj, &wheels_group);
fail_lcds:
        sysfs_remove_group(&sdp->kobj, &lcds_group);
fail_leds:
        sysfs_remove_group(&sdp->kobj, &leds_group);



fail_reg:
        kobject_unregister(&sdp->kobj);
fail:
        fs_err(sdp, "error %d adding sysfs files", error);
        return error;
}

// int sysfs_create_link(struct kobject *kobj,
//			  struct kobject *target,
//			  char *name);

void alphatrack_sys_fs_del(struct device *sdp)
{
        sysfs_remove_group(&sdp->kobj, &touchsurfaces_group);
        sysfs_remove_group(&sdp->kobj, &buttons_group);
        sysfs_remove_group(&sdp->kobj, &faders_group);
        sysfs_remove_group(&sdp->kobj, &lcds_group);
        sysfs_remove_group(&sdp->kobj, &wheels_group);
        sysfs_remove_group(&sdp->kobj, &leds_group)
//void sysfs_remove_link(struct kobject *kobj, char *name);
					kobject_unregister(&sdp->kobj);
}

int alphatrack_sys_init(void)
{
        alphatrack_sys_margs = NULL;
        spin_lock_init(&alphatrack_sys_margs_lock);
        return kset_register(&alphatrack_kset);
}

void alphatrack_sys_uninit(void)
{
        kfree(alphatrack_sys_margs);
        kset_unregister(&alphatrack_kset);
}


//decl_subsys(char *name, struct kobj_type *type,
//                struct kset_hotplug_ops *hotplug_ops);

/* End of all the crazy sysfs stuff */

#define SYSEX_INQUIRE signed char *SYSEX_INQUIRE[] = { 0xf0,0x7e,0x00,0x06,0x01,0x17 };

#define COMMAND(NAME,CONT_NAME)  { BUTTONMASK_##NAME, ((0x90 << 8) | CONT_NAME), ((0x90 << 8) | CONT_NAME), #NAME, NAME ## _set }
#define ROTARY(NAME,CONT_NAME)  { FADER_##NAME, ((0xb0 << 8) | CONT_NAME), ((0xb0 << 8) | CONT_NAME), #NAME, NAME ## _set }
#define SPOSITION(NAME,CONT_NAME)  { BUTTON_##NAME ((0xe9 << 8) | CONT_NAME), #NAME, NAME ## _set }
#define ENDCOMMAND { 0,NULL,0,NULL,NULL}

/* Now that we've generated all our callbacks */

static struct buttonmap_t buttonmap[] =
     {
       COMMAND (REWIND,0x5b),
       COMMAND (FASTFORWARD,0x5c),
       COMMAND (STOP,0x5d),
       COMMAND (PLAY,0x5e),
       COMMAND (RECORD,0x5f),
       COMMAND (SHIFT,0x46),
       COMMAND (TRACKLEFT,0x57),
       COMMAND (TRACKRIGHT,0x58),
       COMMAND (LOOP,0x56),
       COMMAND (FLIP,0x32),
       COMMAND (MUTE,0x10),
       COMMAND (F1,0x36),
       COMMAND (F2,0x37),
       COMMAND (F3,0x38),
       COMMAND (F4,0x39),
       COMMAND (SOLO,0x08),
       COMMAND (ANY,0x73),
       COMMAND (PAN,0x2a),
       COMMAND (SEND,0x29),
       COMMAND (EQ,0x2c),
       COMMAND (PLUGIN,0x2b),
       COMMAND (AUTO,0x4a),
       COMMAND (TRACKREC,0x00),
       COMMAND (FOOTSWITCH1,0x67),
       COMMAND (KNOBTOUCH1,0x78),
       COMMAND (KNOBPUSH1,0x20),
       ROTARY  (KNOBTURN1,0x10),
       COMMAND (KNOBTOUCH2,0x79),
       COMMAND (KNOBPUSH2,0x21),
       ROTARY  (KNOBTURN2,0x11),
       COMMAND (KNOBTOUCH3,0x7a),
       COMMAND (KNOBPUSH3,0x22),
       ROTARY  (KNOBTURN3,0x12),
       COMMAND (FADERTOUCH1,0x68),
       COMMAND (STRIPTOUCH1,0x74),
       COMMAND (STRIPTOUCH2,0x6b),
       SPOSITION (STRIPPOS1,0x00),
       ENDCOMMAND
     };


