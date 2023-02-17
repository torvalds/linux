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
	struct rtrs_srv_path *srv_path;

	srv_path = container_of(kobj, struct rtrs_srv_path, kobj);
	kfree(srv_path);
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
	struct rtrs_srv_path *srv_path;
	struct rtrs_path *s;
	char str[MAXHOSTNAMELEN];

	srv_path = container_of(kobj, struct rtrs_srv_path, kobj);
	s = &srv_path->s;
	if (!sysfs_streq(buf, "1")) {
		rtrs_err(s, "%s: invalid value: '%s'\n",
			  attr->attr.name, buf);
		return -EINVAL;
	}

	sockaddr_to_str((struct sockaddr *)&srv_path->s.dst_addr, str,
			sizeof(str));

	rtrs_info(s, "disconnect for path %s requested\n", str);
	/* first remove sysfs itself to avoid deadlock */
	sysfs_remove_file_self(&srv_path->kobj, &attr->attr);
	close_path(srv_path);

	return count;
}

static struct kobj_attribute rtrs_srv_disconnect_attr =
	__ATTR(disconnect, 0644,
	       rtrs_srv_disconnect_show, rtrs_srv_disconnect_store);

static ssize_t rtrs_srv_hca_port_show(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       char *page)
{
	struct rtrs_srv_path *srv_path;
	struct rtrs_con *usr_con;

	srv_path = container_of(kobj, typeof(*srv_path), kobj);
	usr_con = srv_path->s.con[0];

	return sysfs_emit(page, "%u\n", usr_con->cm_id->port_num);
}

static struct kobj_attribute rtrs_srv_hca_port_attr =
	__ATTR(hca_port, 0444, rtrs_srv_hca_port_show, NULL);

static ssize_t rtrs_srv_hca_name_show(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       char *page)
{
	struct rtrs_srv_path *srv_path;

	srv_path = container_of(kobj, struct rtrs_srv_path, kobj);

	return sysfs_emit(page, "%s\n", srv_path->s.dev->ib_dev->name);
}

static struct kobj_attribute rtrs_srv_hca_name_attr =
	__ATTR(hca_name, 0444, rtrs_srv_hca_name_show, NULL);

static ssize_t rtrs_srv_src_addr_show(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       char *page)
{
	struct rtrs_srv_path *srv_path;
	int cnt;

	srv_path = container_of(kobj, struct rtrs_srv_path, kobj);
	cnt = sockaddr_to_str((struct sockaddr *)&srv_path->s.dst_addr,
			      page, PAGE_SIZE);
	return cnt + sysfs_emit_at(page, cnt, "\n");
}

static struct kobj_attribute rtrs_srv_src_addr_attr =
	__ATTR(src_addr, 0444, rtrs_srv_src_addr_show, NULL);

static ssize_t rtrs_srv_dst_addr_show(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       char *page)
{
	struct rtrs_srv_path *srv_path;
	int len;

	srv_path = container_of(kobj, struct rtrs_srv_path, kobj);
	len = sockaddr_to_str((struct sockaddr *)&srv_path->s.src_addr, page,
			      PAGE_SIZE);
	len += sysfs_emit_at(page, len, "\n");
	return len;
}

static struct kobj_attribute rtrs_srv_dst_addr_attr =
	__ATTR(dst_addr, 0444, rtrs_srv_dst_addr_show, NULL);

static struct attribute *rtrs_srv_path_attrs[] = {
	&rtrs_srv_hca_name_attr.attr,
	&rtrs_srv_hca_port_attr.attr,
	&rtrs_srv_src_addr_attr.attr,
	&rtrs_srv_dst_addr_attr.attr,
	&rtrs_srv_disconnect_attr.attr,
	NULL,
};

static const struct attribute_group rtrs_srv_path_attr_group = {
	.attrs = rtrs_srv_path_attrs,
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

static int rtrs_srv_create_once_sysfs_root_folders(struct rtrs_srv_path *srv_path)
{
	struct rtrs_srv_sess *srv = srv_path->srv;
	int err = 0;

	mutex_lock(&srv->paths_mutex);
	if (srv->dev_ref++) {
		/*
		 * Device needs to be registered only on the first session
		 */
		goto unlock;
	}
	srv->dev.class = rtrs_dev_class;
	err = dev_set_name(&srv->dev, "%s", srv_path->s.sessname);
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
		put_device(&srv->dev);
		goto unlock;
	}
	srv->kobj_paths = kobject_create_and_add("paths", &srv->dev.kobj);
	if (!srv->kobj_paths) {
		err = -ENOMEM;
		pr_err("kobject_create_and_add(): %d\n", err);
		device_del(&srv->dev);
		put_device(&srv->dev);
		goto unlock;
	}
	dev_set_uevent_suppress(&srv->dev, false);
	kobject_uevent(&srv->dev.kobj, KOBJ_ADD);
unlock:
	mutex_unlock(&srv->paths_mutex);

	return err;
}

static void
rtrs_srv_destroy_once_sysfs_root_folders(struct rtrs_srv_path *srv_path)
{
	struct rtrs_srv_sess *srv = srv_path->srv;

	mutex_lock(&srv->paths_mutex);
	if (!--srv->dev_ref) {
		kobject_put(srv->kobj_paths);
		mutex_unlock(&srv->paths_mutex);
		device_del(&srv->dev);
		put_device(&srv->dev);
	} else {
		put_device(&srv->dev);
		mutex_unlock(&srv->paths_mutex);
	}
}

static void rtrs_srv_path_stats_release(struct kobject *kobj)
{
	struct rtrs_srv_stats *stats;

	stats = container_of(kobj, struct rtrs_srv_stats, kobj_stats);

	free_percpu(stats->rdma_stats);

	kfree(stats);
}

static struct kobj_type ktype_stats = {
	.sysfs_ops = &kobj_sysfs_ops,
	.release = rtrs_srv_path_stats_release,
};

static int rtrs_srv_create_stats_files(struct rtrs_srv_path *srv_path)
{
	int err;
	struct rtrs_path *s = &srv_path->s;

	err = kobject_init_and_add(&srv_path->stats->kobj_stats, &ktype_stats,
				   &srv_path->kobj, "stats");
	if (err) {
		rtrs_err(s, "kobject_init_and_add(): %d\n", err);
		kobject_put(&srv_path->stats->kobj_stats);
		return err;
	}
	err = sysfs_create_group(&srv_path->stats->kobj_stats,
				 &rtrs_srv_stats_attr_group);
	if (err) {
		rtrs_err(s, "sysfs_create_group(): %d\n", err);
		goto err;
	}

	return 0;

err:
	kobject_del(&srv_path->stats->kobj_stats);
	kobject_put(&srv_path->stats->kobj_stats);

	return err;
}

int rtrs_srv_create_path_files(struct rtrs_srv_path *srv_path)
{
	struct rtrs_srv_sess *srv = srv_path->srv;
	struct rtrs_path *s = &srv_path->s;
	char str[NAME_MAX];
	int err;
	struct rtrs_addr path = {
		.src = &srv_path->s.dst_addr,
		.dst = &srv_path->s.src_addr,
	};

	rtrs_addr_to_str(&path, str, sizeof(str));
	err = rtrs_srv_create_once_sysfs_root_folders(srv_path);
	if (err)
		return err;

	err = kobject_init_and_add(&srv_path->kobj, &ktype, srv->kobj_paths,
				   "%s", str);
	if (err) {
		rtrs_err(s, "kobject_init_and_add(): %d\n", err);
		goto destroy_root;
	}
	err = sysfs_create_group(&srv_path->kobj, &rtrs_srv_path_attr_group);
	if (err) {
		rtrs_err(s, "sysfs_create_group(): %d\n", err);
		goto put_kobj;
	}
	err = rtrs_srv_create_stats_files(srv_path);
	if (err)
		goto remove_group;

	return 0;

remove_group:
	sysfs_remove_group(&srv_path->kobj, &rtrs_srv_path_attr_group);
put_kobj:
	kobject_del(&srv_path->kobj);
destroy_root:
	kobject_put(&srv_path->kobj);
	rtrs_srv_destroy_once_sysfs_root_folders(srv_path);

	return err;
}

void rtrs_srv_destroy_path_files(struct rtrs_srv_path *srv_path)
{
	if (srv_path->stats->kobj_stats.state_in_sysfs) {
		sysfs_remove_group(&srv_path->stats->kobj_stats,
				   &rtrs_srv_stats_attr_group);
		kobject_del(&srv_path->stats->kobj_stats);
		kobject_put(&srv_path->stats->kobj_stats);
	}

	if (srv_path->kobj.state_in_sysfs) {
		sysfs_remove_group(&srv_path->kobj, &rtrs_srv_path_attr_group);
		kobject_put(&srv_path->kobj);
		rtrs_srv_destroy_once_sysfs_root_folders(srv_path);
	}

}
