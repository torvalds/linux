
#include <linux/cpufreq.h>
#include <linux/amlogic/aml_dvfs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/sched.h>

#define DVFS_DBG(format, args...) \
    if (1) printk(KERN_DEBUG "[DVFS]"format, ##args)

#define DVFS_WARN(format, args...) \
    if (1) printk(KERN_DEBUG "[DVFS]"format, ##args)

#define DEBUG_DVFS      0

DEFINE_MUTEX(driver_mutex);
struct aml_dvfs_master {
    unsigned int                    id;                                 // id of dvfs source
    unsigned int                    table_count;                        // count of dvfs table

    struct aml_dvfs_driver         *driver;                             // voltage scale driver for this source
    struct aml_dvfs                *table;                              // dvfs table
    struct cpufreq_frequency_table *freq_table;
    struct mutex                    mutex; 
#if 0
    struct notifier_block           nb;
#endif
    struct list_head                list;
};

LIST_HEAD(__aml_dvfs_list);

int aml_dvfs_register_driver(struct aml_dvfs_driver *driver)
{
    struct list_head *element;
    struct aml_dvfs_master *master = NULL;
    int    success = 0;

    if (driver == NULL) {
        DVFS_DBG("%s, NULL input of driver\n", __func__);
        return -EINVAL;
    }
    mutex_lock(&driver_mutex);
    list_for_each(element, &__aml_dvfs_list) {
        master = list_entry(element, struct aml_dvfs_master, list);
        if (master && (driver->id_mask & master->id)) {
            if (master->driver == NULL) {                               // no driver support for this dvfs source
                master->driver = driver;
                success = 1;
            } else {
                DVFS_DBG("%s, source id %x has driver %s, reject driver %s\n", 
                         __func__, master->id, master->driver->name, driver->name);    
            }
        }
    }
    if (success) {
        DVFS_DBG("%s, driver %s regist success, mask:%x, source id:%x\n",
                 __func__, driver->name, driver->id_mask, master->id);
    } else {
        DVFS_DBG("%s, driver %s regist failed, mask:%x, source id:%x\n",
                 __func__, driver->name, driver->id_mask, master->id);
    }
    mutex_unlock(&driver_mutex);

    return 0;
}
EXPORT_SYMBOL_GPL(aml_dvfs_register_driver);

int aml_dvfs_unregister_driver(struct aml_dvfs_driver *driver)
{
    struct list_head *element;
    struct aml_dvfs_master *master;
    int ok = 0;

    if (driver == NULL) {
        return -EINVAL;    
    }    
    mutex_lock(&driver_mutex);
    list_for_each(element, &__aml_dvfs_list) {
        master = list_entry(element, struct aml_dvfs_master, list);
        if (master && master->driver == driver) {
            DVFS_DBG("%s, driver %s unregist success\n", __func__, master->driver->name);
            master->driver = NULL;
            ok = 1;
        }
    }
    mutex_unlock(&driver_mutex);
    if (!ok) {
        DVFS_DBG("%s, driver %s not found\n", __func__, driver->name);
    }

    return 0;
}
EXPORT_SYMBOL_GPL(aml_dvfs_unregister_driver);

int aml_dvfs_find_voltage(struct aml_dvfs *table, unsigned int freq, unsigned int *min_uV, unsigned int *max_uV, int count)
{
    int i = 0;

    if (unlikely(freq <= table[0].freq)) {
        *min_uV = table[0].min_uV;
        *max_uV = table[0].max_uV;
        return 0;
    }
    for (i = 0; i < count - 1; i++) {
        if (table[i].freq     <  freq && 
            table[i + 1].freq >= freq) {
            *min_uV = table[i + 1].min_uV;
            *max_uV = table[i + 1].max_uV;
            return 0;
        }
    }
    return -EINVAL;
}

int aml_dvfs_do_voltage_change(struct aml_dvfs_master *master, uint32_t new_freq, uint32_t old_freq, uint32_t flags)
{
    uint32_t id = master->id;
    uint32_t min_uV = 0, max_uV = 0, curr_voltage = 0;
    int      ret = 0;

    if (master->table == NULL) {
        goto error;
    }
    if (master->driver == NULL) {
        goto error;
    }
    /*
     * update voltage 
     */
    if ((flags == AML_DVFS_FREQ_PRECHANGE  && new_freq >= old_freq) ||
        (flags == AML_DVFS_FREQ_POSTCHANGE && new_freq <= old_freq)) {
        if (aml_dvfs_find_voltage(master->table, new_freq, &min_uV, &max_uV, master->table_count) < 0) {
            DVFS_DBG("%s, voltage not found for freq:%d\n", __func__, new_freq);
            goto error;
        }
        if (master->driver->get_voltage) {
            master->driver->get_voltage(id, &curr_voltage);
            if (curr_voltage >= min_uV && curr_voltage <= max_uV) { // in range, do not change
            #if DEBUG_DVFS
                DVFS_WARN("%s, voltage %d is in range of [%d, %d], not change\n", 
                          __func__, curr_voltage, min_uV, max_uV);
            #endif
                goto ok;
            }
        }
        if (master->driver->set_voltage) {
        #if DEBUG_DVFS
            DVFS_WARN("%s, freq from %u to %u, voltage from %u to %u\n", 
                      __func__, old_freq, new_freq, curr_voltage, min_uV);
        #endif
            ret = master->driver->set_voltage(id, min_uV, max_uV);    
        #if DEBUG_DVFS
            DVFS_WARN("%s, set voltage finished\n", __func__);
        #endif
        }
    }
ok:
    return ret;
error:
    return -EINVAL;
}

int aml_dvfs_freq_change(unsigned int id, unsigned int new_freq, unsigned int old_freq, unsigned int flags)
{
    struct aml_dvfs_master  *master;
    struct list_head        *element;
    int ret = 0;

    list_for_each(element, &__aml_dvfs_list) {
        master = list_entry(element, struct aml_dvfs_master, list); 
        if (master->id == id) {
            mutex_lock(&master->mutex);
            ret = aml_dvfs_do_voltage_change(master, new_freq, old_freq, flags);
            mutex_unlock(&master->mutex);
            return ret;
        }
    }
    return ret;
}
EXPORT_SYMBOL_GPL(aml_dvfs_freq_change);

static int aml_dummy_set_voltage(uint32_t id, uint32_t min_uV, uint32_t max_uV)
{
    return 0;
}

struct aml_dvfs_driver aml_dummy_dvfs_driver = {
    .name        = "aml-dumy-dvfs",
    .id_mask     = 0, 
    .set_voltage = aml_dummy_set_voltage, 
    .get_voltage = NULL,
};

static ssize_t dvfs_help(struct class *class, struct class_attribute *attr,   char *buf)
{
    return sprintf(buf, 
                   "HELP:\n"
                   "    echo r [name] > dvfs            ---- read voltage of [name]\n"
                   "    echo w [name] [value] > dvfs    ---- write voltage of [name] to [value]\n"
                   "\n"
                   "EXAMPLE:\n"
                   "    echo r vcck > dvfs              ---- read current voltage of vcck\n"
                   "    echo w vcck 1100000 > dvfs      ---- set voltage of vcck to 1.1v\n"
                   "\n"
                   "Supported names:\n"
                   "    vcck    ---- voltage of ARM core\n"
                   "    vddee   ---- voltage of VDDEE(everything else)\n"
                   "    ddr     ---- voltage of DDR\n"
    );
}

static int get_dvfs_id_by_name(char *str)
{
    if (!strncmp(str, "vcck", 4)) {
        str[4] = '\0';
        return AML_DVFS_ID_VCCK;    
    } else if (!strncmp(str, "vddee", 5)) {
        str[5] = '\0';
        return AML_DVFS_ID_VDDEE;    
    } else if (!strncmp(str, "ddr", 3)) {
        str[3] = '\0';
        return AML_DVFS_ID_DDR;    
    }
    return -1;
}

static ssize_t dvfs_class_write(struct class *class, struct class_attribute *attr,   const char *buf, size_t count)
{
    int ret = -1;
    int  id, i;
    unsigned int uV;
    char *arg[3] = {}, *para, *buf_work, *p;
    struct aml_dvfs_master  *master;
    struct list_head        *element;

    buf_work = kstrdup(buf, GFP_KERNEL);
    p = buf_work;
    for (i = 0; i < 3; i++) {
        para = strsep(&p, " ");
        if (para == NULL) {
            break;
        }    
        arg[i] = para;
    }    
    if (i < 2 || i > 3) { 
        ret = 1; 
        goto error;
    } 

    switch (arg[0][0]) {
    case 'r':
        id = get_dvfs_id_by_name(arg[1]);
        if (id < 0) {
            goto error;    
        }
        list_for_each(element, &__aml_dvfs_list) {
            master = list_entry(element, struct aml_dvfs_master, list); 
            if (master->id == id) {
                mutex_lock(&master->mutex);
                if (master->driver->get_voltage) {
                    ret = master->driver->get_voltage(id, &uV);
                } 
                mutex_unlock(&master->mutex);
            }
        }
        if (ret < 0) {
            printk("get voltage of %s failed\n", arg[1]);    
        } else {
            printk("voltage of %s is %d\n", arg[1], uV); 
        }
        break;

    case 'w':
        if (i != 3) {
            goto error;    
        }
        id = get_dvfs_id_by_name(arg[1]);
        if (id < 0) {
            goto error;    
        }
        uV = simple_strtoul(arg[2], NULL, 10); 
        list_for_each(element, &__aml_dvfs_list) {
            master = list_entry(element, struct aml_dvfs_master, list); 
            if (master->id == id) {
                mutex_lock(&master->mutex);
                if (master->driver->set_voltage) {
                    ret = master->driver->set_voltage(id, uV, uV);
                } 
                mutex_unlock(&master->mutex);
            }
        }
        if (ret < 0) {
            printk("set %s to %d uV failed\n", arg[1], uV);    
        } else {
            printk("set %s to %d uV success\n", arg[1], uV);    
        }
        break;
    }
error:
    kfree(buf_work);
    if (ret) {
        printk(" error\n");    
    }
    return count; 
}

static CLASS_ATTR(dvfs, S_IWUSR | S_IRUGO, dvfs_help, dvfs_class_write);
struct class *aml_dvfs_class;

struct cpufreq_frequency_table *aml_dvfs_get_freq_table(unsigned int id)
{
    struct aml_dvfs_master  *master;
    struct list_head        *element;

    list_for_each(element, &__aml_dvfs_list) {
        master = list_entry(element, struct aml_dvfs_master, list);
        if (master->id == id) {                                         // match
            return master->freq_table;
        }
    }
    return NULL;
}
EXPORT_SYMBOL_GPL(aml_dvfs_get_freq_table);

static int aml_dvfs_init_for_master(struct aml_dvfs_master *master)
{
    int ret = 0;
    int i, size;

    mutex_init(&master->mutex);
    size = sizeof(struct cpufreq_frequency_table) * (master->table_count + 1);
    master->freq_table = kzalloc(size, GFP_KERNEL);
    if (master->freq_table == NULL) {
        printk("%s, allocate buffer failed\n", __func__); 
        return -ENOMEM;
    }
    for (i = 0; i < master->table_count; i++) {
        master->freq_table[i].index     = i;
        master->freq_table[i].frequency = master->table[i].freq;
    } 
    master->freq_table[i].index     = i;
    master->freq_table[i].frequency = CPUFREQ_TABLE_END;                // end flag of this table;

    return ret;
}

static int aml_dvfs_probe(struct platform_device *pdev)
{
    struct device_node     *dvfs_node = pdev->dev.of_node;
    struct device_node     *child;
    struct aml_dvfs_master *master;
    struct aml_dvfs        *table;
    int   err;
    int   id = 0;
    int   table_cnt;

    for_each_child_of_node(dvfs_node, child) {
        DVFS_DBG("%s, child name:%s\n", __func__, child->name);
        err = of_property_read_u32(child, "dvfs_id", &id);              // read dvfs id
        if (err) {
            DVFS_DBG("%s, get property 'dvfs_name' failed\n", __func__);
            continue;
        }
        master = kzalloc(sizeof(*master), GFP_KERNEL);
        if (master == NULL) {
            DVFS_DBG("%s, allocate memory failed\n", __func__);
            return -ENOMEM;
        }
        master->id = id;
        err = of_property_read_u32(child, "table_count", &table_cnt);   // get table count
        if (err) {
            DVFS_DBG("%s, get property 'table_count' failed\n", __func__);    
            continue;
        }
    #if DEBUG_DVFS
        DVFS_DBG("%s, get table_count = %d\n", __func__, table_cnt);
    #endif
        master->table_count = table_cnt;
        table = kzalloc(sizeof(*table) * table_cnt, GFP_KERNEL);
        if (table == NULL) {
            DVFS_DBG("%s, allocate memory failed\n", __func__);
            return -ENOMEM;
        }
        /*
         * 
         */
        err = of_property_read_u32_array(child, 
                                         "dvfs_table", 
                                         (uint32_t *)table, 
                                         (sizeof(*table) * table_cnt) / sizeof(unsigned int));
        DVFS_DBG("dvfs table of %s is:\n", child->name);
        DVFS_DBG("%9s, %9s, %9s\n", "freq", "min_uV", "max_uV");
        for (id = 0; id < table_cnt; id++) {
            DVFS_DBG("%9d, %9d, %9d\n", table[id].freq, table[id].min_uV, table[id].max_uV);
        }
        if (err) {
            DVFS_DBG("%s, get property 'dvfs_table' failed\n", __func__);    
            continue;
        }
        master->table = table;
        list_add_tail(&master->list, &__aml_dvfs_list);
        if (aml_dvfs_init_for_master(master)) {
            return -EINVAL;    
        }
        err = of_property_read_bool(child, "change-frequent-only");
        if (err) {
            aml_dummy_dvfs_driver.id_mask = id;
            aml_dvfs_register_driver(&aml_dummy_dvfs_driver); 
        }
    }

    aml_dvfs_class = class_create(THIS_MODULE, "dvfs");
    return class_create_file(aml_dvfs_class, &class_attr_dvfs);
}

static int aml_dvfs_remove(struct platform_device *pdev)
{
    struct list_head *element;
    struct aml_dvfs_master *master;

    class_destroy(aml_dvfs_class); 
    list_for_each(element, &__aml_dvfs_list) {
        master = list_entry(element, struct aml_dvfs_master, list);
        kfree(master->freq_table);
        kfree(master->table);
        kfree(master);
    }
    return 0;
}

static const struct of_device_id aml_dvfs_dt_match[] = { 
    {   
        .compatible = "amlogic, amlogic-dvfs",
    },  
    {}  
};

static  struct platform_driver aml_dvfs_prober= { 
    .probe      = aml_dvfs_probe,
    .remove     = aml_dvfs_remove,
    .driver     = { 
        .name   = "amlogic-dvfs",
        .owner  = THIS_MODULE,
        .of_match_table = aml_dvfs_dt_match,
    },  
};

static int __init aml_dvfs_init(void)
{
    int ret;
    printk("call %s in\n", __func__);
    ret = platform_driver_register(&aml_dvfs_prober);
    return ret;
}

static void __exit aml_dvfs_exit(void)
{
    platform_driver_unregister(&aml_dvfs_prober);
}

arch_initcall(aml_dvfs_init);
module_exit(aml_dvfs_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Amlogic DVFS interface driver");
