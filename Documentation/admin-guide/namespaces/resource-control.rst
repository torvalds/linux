====================================
User namespaces and resource control
====================================

The kernel contains many kinds of objects that either don't have
individual limits or that have limits which are ineffective when
a set of processes is allowed to switch their UID. On a system
where the admins don't trust their users or their users' programs,
user namespaces expose the system to potential misuse of resources.

In order to mitigate this, we recommend that admins enable memory
control groups on any system that enables user namespaces.
Furthermore, we recommend that admins configure the memory control
groups to limit the maximum memory usable by any untrusted user.

Memory control groups can be configured by installing the libcgroup
package present on most distros editing /etc/cgrules.conf,
/etc/cgconfig.conf and setting up libpam-cgroup.
