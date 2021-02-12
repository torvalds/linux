// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RDMA Transport Layer
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME " L" __stringify(__LINE__) ": " fmt

#include "rtrs-pri.h"
#include "rtrs-srv.h"
#include "rtrs-log.h"

static void rtrs_srv_release(struct kobject *kobj)
{
	struct rtrs_srv_sess *sess;

	sess = container_of(kobj, struct rtrs_srv_sess, kobj);
	kfree(sess);
}

static struct kobj_type ktype = {
	.sysfs_ops	= &kobj_sysfs_ops,
	.release	= rtrs_srv_release,
};

static ssize_t rtrs_srv_disconnect_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "Usage: echo 1 > %s\n", attr->attr.name);
}

static ssize_t rtrs_srv_disconnect_store(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  const char *buf, size_t count)
{
	struct rtrs_srv_sess *sess;
	struct rtrs_sess *s;
	char str[MAXHOSTNAMELEN];

	sess = container_of(kobj, struct rtrs_srv_sess, kobj);
	s = &sess->s;
	if (!sysfs_streq(buf, "1")) {
		rtrs_err(s, "%s: invalid value: '%s'\n",
			  attr->attr.name, buf);
		return -EINVAL;
	}

	sockaddr_to_str((struct sockaddr *)&sess->s.dst_addr, str, sizeof(str));

	rtrs_info(s, "disconnect for path %s requested\n", str);
	/* first remove sysfs itself to avoid deadlock */
	sysfs_remove_file_self(&sess->kobj, &attr->attr);
	close_sess(sess);

	return count;
}

static struct kobj_attribute rtrs_srv_disconnect_attr =
	__ATTR(disconnect, 0644,
	       rtrs_srv_disconnect_show, rtrs_srv_disconnect_store);

static ssize_t rtrs_srv_hca_port_show(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       char *page)
{
	struct rtrs_srv_sess *sess;
	struct rtrs_con *usr_con;

	sess = container_of(kobj, typeof(*sess), kobj);
	usr_con = sess->s.con[0];

	return sysfs_emit(page, "%u\n", usr_con->cm_id->port_num);
}

static struct kobj_attribute rtrs_srv_hca_port_attr =
	__ATTR(hca_port, 0444, rtrs_srv_hca_port_show, NULL);

static ssize_t rtrs_srv_hca_name_show(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       char *page)
{
	struct rtrs_srv_sess *sess;

	sess = container_of(kobj, struct rtrs_srv_sess, kobj);

	return sysfs_emit(page, "%s\n", sess->s.dev->ib_dev->name);
}

static struct kobj_attribute rtrs_srv_hca_name_attr =
	__ATTR(hca_name, 0444, rtrs_srv_hca_name_show, NULL);

static ssize_t rtrs_srv_src_addr_show(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       char *page)
{
	struct rtrs_srv_sess *sess;
	int cnt;

	sess = container_of(kobj, struct rtrs_srv_sess, kobj);
	cnt = sockaddr_to_str((struct sockaddr *)&sess->s.dst_addr,
			      page, PAGE_SIZE);
	return cnt + scnprintf(page + cnt, PAGE_SIZE - cnt, "\n");
}

static struct kobj_attribute rtrs_srv_src_addr_attr =
	__ATTR(src_addr, 0444, rtrs_srv_src_addr_show, NULL);

static ssize_t rtrs_srv_dst_addr_show(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       char *page)
{
	struct rtrs_srv_sess *sess;
	int len;

	sess = container_of(kobj, struct rtrs_srv_sess, kobj);
	len = sockaddr_to_str((struct sockaddr *)&sess->s.src_addr, page,
			      PAGE_SIZE);
	len += sysfs_emit_at(page, len, "\n");
	return len;
}

static struct kobj_attribute rtrs_srv_dst_addr_attr =
	__ATTR(dst_addr, 0444, rtrs_srv_dst_addr_show, NULL);

static struct attribute *rtrs_srv_sess_attrs[] = {
	&rtrs_srv_hca_name_attr.attr,
	&rtrs_srv_hca_port_attr.attr,
	&rtrs_srv_src_addr_attr.attr,
	&rtrs_srv_dst_addr_attr.attr,
	&rtrs_srv_disconnect_attr.attr,
	NULL,
};

static const struct attribute_group rtrs_srv_sess_attr_group = {
	.attrs = rtrs_srv_sess_attrs,
};

STAT_ATTR(struct rtrs_srv_stats, rdma,
	  rtrs_srv_stats_rdma_to_str,
	  rtrs_srv_reset_rdma_stats);

static struct attribute *rtrs_srv_stats_attrs[] = {
	&rdma_attr.attr,
	NULL,
};

static const struct attribute_group rtrs_srv_stats_attr_group = {
	.attrs = rtrs_srv_stats_attrs,
};

static int rtrs_srv_create_once_sysfs_root_folders(struct rtrs_srv_sess *sess)
{
	struct rtrs_srv *srv = sess->srv;
	int err = 0;

	mutex_lock(&srv->paths_mutex);
	if (srv->dev_ref++) {
		/*
		 * Device needs to be registered only on the first session
		 */
		goto unlock;
	}
	srv->dev.class = rtrs_dev_class;
	err = dev_set_name(&srv->dev, "%s", sess->s.sessname);
	if (err)
		goto unlock;

	/*
	 * Suppress user space notification until
	 * sysfs files are created
	 */
	dev_set_uevent_suppress(&srv->dev, true);
	err = device_add(&srv->dev);
	if (err) {
		pr_err("device_add(): %d\n", err);
		goto put;
	}
	srv->kobj_paths = kobject_create_and_add("paths", &srv->dev.kobj);
	if (!srv->kobj_paths) {
		err = -ENOMEM;
		pr_err("kobject_create_and_add(): %d\n", err);
		device_del(&srv->dev);
		goto unlock;
	}
	dev_set_uevent_suppress(&srv->dev, false);
	kobject_uevent(&srv->dev.kobj, KOBJ_ADD);
	goto unlock;

put:
	put_device(&srv->dev);
unlock:
	mutex_unlock(&srv->paths_mutex);

	return err;
}

static void
rtrs_srv_destroy_once_sysfs_root_folders(struct rtrs_srv_sess *sess)
{
	struct rtrs_srv *srv = sess->srv;

	mutex_lock(&srv->paths_mutex);
	if (!--srv->dev_ref) {
		kobject_del(srv->kobj_paths);
		kobject_put(srv->kobj_paths);
		mutex_unlock(&srv->paths_mutex);
		device_del(&srv->dev);
	} else {
		mutex_unlock(&srv->paths_mutex);
	}
}

static void rtrs_srv_sess_stats_release(struct kobject *kobj)
{
	struct rtrs_srv_stats *stats;

	stats = container_of(kobj, struct rtrs_srv_stats, kobj_stats);

	kfree(stats);
}

static struct kobj_type ktype_stats = {
	.sysfs_ops = &kobj_sysfs_ops,
	.release = rtrs_srv_sess_stats_release,
};

static int rtrs_srv_create_stats_files(struct rtrs_srv_sess *sess)
{
	int err;
	struct rtrs_sess *s = &sess->s;

	err = kobject_init_and_add(&sess->stats->kobj_stats, &ktype_stats,
				   &sess->kobj, "stats");
	if (err) {
		rtrs_err(s, "kobject_init_and_add(): %d\n", err);
		kobject_put(&sess->stats->kobj_stats);
		return err;
	}
	err = sysfs_create_group(&sess->stats->kobj_stats,
				 &rtrs_srv_stats_attr_group);
	if (err) {
		rtrs_err(s, "sysfs_create_group(): %d\n", err);
		goto err;
	}

	return 0;

err:
	kobject_del(&sess->stats->kobj_stats);
	kobject_put(&sess->stats->kobj_stats);

	return err;
}

int rtrs_srv_create_sess_files(struct rtrs_srv_sess *sess)
{
	struct rtrs_srv *srv = sess->srv;
	struct rtrs_sess *s = &sess->s;
	char str[NAME_MAX];
	int err, cnt;

	cnt = sockaddr_to_str((struct sockaddr *)&sess->s.dst_addr,
			      str, sizeof(str));
	cnt += scnprintf(str + cnt, sizeof(str) - cnt, "@");
	sockaddr_to_str((struct sockaddr *)&sess->s.src_addr,
			str + cnt, sizeof(str) - cnt);

	err = rtrs_srv_create_once_sysfs_root_folders(sess);
	if (err)
		return err;

	err = kobject_init_and_add(&sess->kobj, &ktype, srv->kobj_paths,
				   "%s", str);
	if (err) {
		rtrs_err(s, "kobject_init_and_add(): %d\n", err);
		goto destroy_root;
	}
	err = sysfs_create_group(&sess->kobj, &rtrs_srv_sess_attr_group);
	if (err) {
		rtrs_err(s, "sysfs_create_group(): %d\n", err);
		goto put_kobj;
	}
	err = rtrs_srv_create_stats_files(sess);
	if (err)
		goto remove_group;

	return 0;

remove_group:
	sysfs_remove_group(&sess->kobj, &rtrs_srv_sess_attr_group);
put_kobj:
	kobject_del(&sess->kobj);
destroy_root:
	kobject_put(&sess->kobj);
	rtrs_srv_destroy_once_sysfs_root_folders(sess);

	return err;
}

void rtrs_srv_destroy_sess_files(struct rtrs_srv_sess *sess)
{
	if (sess->kobj.state_in_sysfs) {
		kobject_del(&sess->stats->kobj_stats);
		kobject_put(&sess->stats->kobj_stats);
		sysfs_remove_group(&sess->kobj, &rtrs_srv_sess_attr_group);
		kobject_put(&sess->kobj);

		rtrs_srv_destroy_once_sysfs_root_folders(sess);
	}
}
