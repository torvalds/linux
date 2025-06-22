#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/timekeeping.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/ktime.h>
#include <linux/err.h>

#define DEVICE_NAME   "mlcore"
#define PROC_RESULT   "mlcore_result"
#define PROC_TRAIN    "mlcore_train"
#define LOG_FILE_PATH "/var/log/mlcore.log"
#define MAJOR_NUM     104

#define IOCTL_SEND_INTS        _IOW(MAJOR_NUM, 0, int[3])
#define IOCTL_SET_RESULT       _IOW(MAJOR_NUM, 1, int)
#define IOCTL_GET_RESULT       _IOR(MAJOR_NUM, 2, int *)
#define IOCTL_ADD_TRAINING     _IOW(MAJOR_NUM, 3, struct train_entry)
#define IOCTL_CLEAR_TRAINING   _IO(MAJOR_NUM, 4)

#define HISTORY_SIZE 10
#define MAX_TRAINING_ENTRIES 50

struct prediction_entry {
    int result;
    struct timespec64 timestamp;
};

struct train_entry {
    int features[3]; // fixed-point representation: value * 100
    int label;
};

static int features[3];
static struct prediction_entry history[HISTORY_SIZE];
static int hist_index = 0;
static int hist_count = 0;
static int latest_result = -1;

static struct train_entry training_data[MAX_TRAINING_ENTRIES];
static int train_count = 0;

static void log_to_file(int result) {
    struct file *filp;
    struct timespec64 ts;
    struct tm tm;
    char log_msg[128];
    ssize_t written;
    loff_t pos = 0;

    ktime_get_real_ts64(&ts);
    time64_to_tm(ts.tv_sec, 0, &tm);

    snprintf(log_msg, sizeof(log_msg),
             "%04ld-%02d-%02d %02d:%02d:%02d - Prediction: %d\n",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec,
             result);

    filp = filp_open(LOG_FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (IS_ERR(filp)) {
        printk(KERN_ERR "[mlcore] Failed to open log file.\n");
        return;
    }

    written = kernel_write(filp, log_msg, strlen(log_msg), &pos);
    if (written < 0) {
        printk(KERN_ERR "[mlcore] Failed to write to log file.\n");
    }

    filp_close(filp, NULL);
}

static long dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    switch (cmd) {
        case IOCTL_SEND_INTS:
            if (copy_from_user(features, (int __user *)arg, sizeof(features)))
                return -EFAULT;
            printk(KERN_INFO "[mlcore] Received features: %d.%02d %d.%02d %d.%02d\n",
                   features[0]/100, abs(features[0]%100),
                   features[1]/100, abs(features[1]%100),
                   features[2]/100, abs(features[2]%100));
            return 0;

        case IOCTL_SET_RESULT: {
            int result;
            if (copy_from_user(&result, (int __user *)arg, sizeof(int)))
                return -EFAULT;

            latest_result = result;

            history[hist_index].result = result;
            ktime_get_real_ts64(&history[hist_index].timestamp);
            hist_index = (hist_index + 1) % HISTORY_SIZE;
            if (hist_count < HISTORY_SIZE) hist_count++;

            log_to_file(result);
            printk(KERN_INFO "[mlcore] Prediction set: %d\n", result);
            return 0;
        }

        case IOCTL_GET_RESULT:
            if (copy_to_user((int __user *)arg, &latest_result, sizeof(int)))
                return -EFAULT;
            return 0;

        case IOCTL_ADD_TRAINING: {
            struct train_entry entry;
            if (copy_from_user(&entry, (struct train_entry __user *)arg, sizeof(entry)))
                return -EFAULT;
            if (train_count < MAX_TRAINING_ENTRIES) {
                training_data[train_count++] = entry;
                printk(KERN_INFO "[mlcore] Training data added: %d.%02d %d.%02d %d.%02d => %d\n",
                       entry.features[0]/100, abs(entry.features[0]%100),
                       entry.features[1]/100, abs(entry.features[1]%100),
                       entry.features[2]/100, abs(entry.features[2]%100),
                       entry.label);
            } else {
                printk(KERN_WARNING "[mlcore] Training buffer full!\n");
            }
            return 0;
        }

        case IOCTL_CLEAR_TRAINING:
            train_count = 0;
            printk(KERN_INFO "[mlcore] Training buffer cleared\n");
            return 0;

        default:
            return -EINVAL;
    }
}

static int proc_result_show(struct seq_file *m, void *v) {
    int i, idx;
    struct prediction_entry *entry;
    struct tm tm;

    seq_printf(m, "Current Prediction: %d\n", latest_result);
    seq_printf(m, "Prediction History (latest %d):\n", hist_count);

    for (i = 0; i < hist_count; i++) {
        idx = (hist_index + HISTORY_SIZE - hist_count + i) % HISTORY_SIZE;
        entry = &history[idx];
        time64_to_tm(entry->timestamp.tv_sec, 0, &tm);
        seq_printf(m, "  [%d]: %d at %04ld-%02d-%02d %02d:%02d:%02d\n",
                   i, entry->result,
                   tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                   tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
    return 0;
}

static int proc_train_show(struct seq_file *m, void *v) {
    int i;
    for (i = 0; i < train_count; i++) {
        seq_printf(m, "[%d]: %d.%02d %d.%02d %d.%02d => %d\n", i,
                   training_data[i].features[0]/100, abs(training_data[i].features[0]%100),
                   training_data[i].features[1]/100, abs(training_data[i].features[1]%100),
                   training_data[i].features[2]/100, abs(training_data[i].features[2]%100),
                   training_data[i].label);
    }
    return 0;
}

static int proc_result_open(struct inode *inode, struct file *file) {
    return single_open(file, proc_result_show, NULL);
}

static int proc_train_open(struct inode *inode, struct file *file) {
    return single_open(file, proc_train_show, NULL);
}

static const struct proc_ops proc_result_fops = {
    .proc_open    = proc_result_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static const struct proc_ops proc_train_fops = {
    .proc_open    = proc_train_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static int dev_open(struct inode *inode, struct file *file) { return 0; }
static int dev_release(struct inode *inode, struct file *file) { return 0; }

static struct file_operations fops = {
    .unlocked_ioctl = dev_ioctl,
    .open = dev_open,
    .release = dev_release,
};

static int __init mlcore_init(void) {
    register_chrdev(MAJOR_NUM, DEVICE_NAME, &fops);
    proc_create(PROC_RESULT, 0, NULL, &proc_result_fops);
    proc_create(PROC_TRAIN, 0, NULL, &proc_train_fops);
    printk(KERN_INFO "[mlcore] Loaded with fixed-point logging.\n");
    return 0;
}

static void __exit mlcore_exit(void) {
    unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
    remove_proc_entry(PROC_RESULT, NULL);
    remove_proc_entry(PROC_TRAIN, NULL);
    printk(KERN_INFO "[mlcore] Unloaded.\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sasikumar C <sasikumar4289@gmail.com>");
MODULE_DESCRIPTION("ML driver using fixed-point instead of float");
module_init(mlcore_init);
module_exit(mlcore_exit);
