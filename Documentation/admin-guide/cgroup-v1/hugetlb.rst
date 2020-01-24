==================
HugeTLB Controller
==================

The HugeTLB controller allows to limit the HugeTLB usage per control group and
enforces the controller limit during page fault. Since HugeTLB doesn't
support page reclaim, enforcing the limit at page fault time implies that,
the application will get SIGBUS signal if it tries to access HugeTLB pages
beyond its limit. This requires the application to know beforehand how much
HugeTLB pages it would require for its use.

HugeTLB controller can be created by first mounting the cgroup filesystem.

# mount -t cgroup -o hugetlb none /sys/fs/cgroup

With the above step, the initial or the parent HugeTLB group becomes
visible at /sys/fs/cgroup. At bootup, this group includes all the tasks in
the system. /sys/fs/cgroup/tasks lists the tasks in this cgroup.

New groups can be created under the parent group /sys/fs/cgroup::

  # cd /sys/fs/cgroup
  # mkdir g1
  # echo $$ > g1/tasks

The above steps create a new group g1 and move the current shell
process (bash) into it.

Brief summary of control files::

 hugetlb.<hugepagesize>.limit_in_bytes     # set/show limit of "hugepagesize" hugetlb usage
 hugetlb.<hugepagesize>.max_usage_in_bytes # show max "hugepagesize" hugetlb  usage recorded
 hugetlb.<hugepagesize>.usage_in_bytes     # show current usage for "hugepagesize" hugetlb
 hugetlb.<hugepagesize>.failcnt		   # show the number of allocation failure due to HugeTLB limit

For a system supporting three hugepage sizes (64k, 32M and 1G), the control
files include::

  hugetlb.1GB.limit_in_bytes
  hugetlb.1GB.max_usage_in_bytes
  hugetlb.1GB.usage_in_bytes
  hugetlb.1GB.failcnt
  hugetlb.64KB.limit_in_bytes
  hugetlb.64KB.max_usage_in_bytes
  hugetlb.64KB.usage_in_bytes
  hugetlb.64KB.failcnt
  hugetlb.32MB.limit_in_bytes
  hugetlb.32MB.max_usage_in_bytes
  hugetlb.32MB.usage_in_bytes
  hugetlb.32MB.failcnt
