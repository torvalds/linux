.. SPDX-License-Identifier: GPL-2.0

.. _cpumasks-header-label:

==================
BPF cpumask kfuncs
==================

1. Introduction
===============

``struct cpumask`` is a bitmap data structure in the kernel whose indices
reflect the CPUs on the system. Commonly, cpumasks are used to track which CPUs
a task is affinitized to, but they can also be used to e.g. track which cores
are associated with a scheduling domain, which cores on a machine are idle,
etc.

BPF provides programs with a set of :ref:`kfuncs-header-label` that can be
used to allocate, mutate, query, and free cpumasks.

2. BPF cpumask objects
======================

There are two different types of cpumasks that can be used by BPF programs.

2.1 ``struct bpf_cpumask *``
----------------------------

``struct bpf_cpumask *`` is a cpumask that is allocated by BPF, on behalf of a
BPF program, and whose lifecycle is entirely controlled by BPF. These cpumasks
are RCU-protected, can be mutated, can be used as kptrs, and can be safely cast
to a ``struct cpumask *``.

2.1.1 ``struct bpf_cpumask *`` lifecycle
----------------------------------------

A ``struct bpf_cpumask *`` is allocated, acquired, and released, using the
following functions:

.. kernel-doc:: kernel/bpf/cpumask.c
  :identifiers: bpf_cpumask_create

.. kernel-doc:: kernel/bpf/cpumask.c
  :identifiers: bpf_cpumask_acquire

.. kernel-doc:: kernel/bpf/cpumask.c
  :identifiers: bpf_cpumask_release

For example:

.. code-block:: c

        struct cpumask_map_value {
                struct bpf_cpumask __kptr_ref * cpumask;
        };

        struct array_map {
                __uint(type, BPF_MAP_TYPE_ARRAY);
                __type(key, int);
                __type(value, struct cpumask_map_value);
                __uint(max_entries, 65536);
        } cpumask_map SEC(".maps");

        static int cpumask_map_insert(struct bpf_cpumask *mask, u32 pid)
        {
                struct cpumask_map_value local, *v;
                long status;
                struct bpf_cpumask *old;
                u32 key = pid;

                local.cpumask = NULL;
                status = bpf_map_update_elem(&cpumask_map, &key, &local, 0);
                if (status) {
                        bpf_cpumask_release(mask);
                        return status;
                }

                v = bpf_map_lookup_elem(&cpumask_map, &key);
                if (!v) {
                        bpf_cpumask_release(mask);
                        return -ENOENT;
                }

                old = bpf_kptr_xchg(&v->cpumask, mask);
                if (old)
                        bpf_cpumask_release(old);

                return 0;
        }

        /**
         * A sample tracepoint showing how a task's cpumask can be queried and
         * recorded as a kptr.
         */
        SEC("tp_btf/task_newtask")
        int BPF_PROG(record_task_cpumask, struct task_struct *task, u64 clone_flags)
        {
                struct bpf_cpumask *cpumask;
                int ret;

                cpumask = bpf_cpumask_create();
                if (!cpumask)
                        return -ENOMEM;

                if (!bpf_cpumask_full(task->cpus_ptr))
                        bpf_printk("task %s has CPU affinity", task->comm);

                bpf_cpumask_copy(cpumask, task->cpus_ptr);
                return cpumask_map_insert(cpumask, task->pid);
        }

----

2.1.1 ``struct bpf_cpumask *`` as kptrs
---------------------------------------

As mentioned and illustrated above, these ``struct bpf_cpumask *`` objects can
also be stored in a map and used as kptrs. If a ``struct bpf_cpumask *`` is in
a map, the reference can be removed from the map with bpf_kptr_xchg(), or
opportunistically acquired with bpf_cpumask_kptr_get():

.. kernel-doc:: kernel/bpf/cpumask.c
  :identifiers: bpf_cpumask_kptr_get

Here is an example of a ``struct bpf_cpumask *`` being retrieved from a map:

.. code-block:: c

	/* struct containing the struct bpf_cpumask kptr which is stored in the map. */
	struct cpumasks_kfunc_map_value {
		struct bpf_cpumask __kptr_ref * bpf_cpumask;
	};

	/* The map containing struct cpumasks_kfunc_map_value entries. */
	struct {
		__uint(type, BPF_MAP_TYPE_ARRAY);
		__type(key, int);
		__type(value, struct cpumasks_kfunc_map_value);
		__uint(max_entries, 1);
	} cpumasks_kfunc_map SEC(".maps");

	/* ... */

	/**
	 * A simple example tracepoint program showing how a
	 * struct bpf_cpumask * kptr that is stored in a map can
	 * be acquired using the bpf_cpumask_kptr_get() kfunc.
	 */
	SEC("tp_btf/cgroup_mkdir")
	int BPF_PROG(cgrp_ancestor_example, struct cgroup *cgrp, const char *path)
	{
		struct bpf_cpumask *kptr;
		struct cpumasks_kfunc_map_value *v;
		u32 key = 0;

		/* Assume a bpf_cpumask * kptr was previously stored in the map. */
		v = bpf_map_lookup_elem(&cpumasks_kfunc_map, &key);
		if (!v)
			return -ENOENT;

		/* Acquire a reference to the bpf_cpumask * kptr that's already stored in the map. */
		kptr = bpf_cpumask_kptr_get(&v->cpumask);
		if (!kptr)
			/* If no bpf_cpumask was present in the map, it's because
			 * we're racing with another CPU that removed it with
			 * bpf_kptr_xchg() between the bpf_map_lookup_elem()
			 * above, and our call to bpf_cpumask_kptr_get().
			 * bpf_cpumask_kptr_get() internally safely handles this
			 * race, and will return NULL if the cpumask is no longer
			 * present in the map by the time we invoke the kfunc.
			 */
			return -EBUSY;

		/* Free the reference we just took above. Note that the
		 * original struct bpf_cpumask * kptr is still in the map. It will
		 * be freed either at a later time if another context deletes
		 * it from the map, or automatically by the BPF subsystem if
		 * it's still present when the map is destroyed.
		 */
		bpf_cpumask_release(kptr);

		return 0;
	}

----

2.2 ``struct cpumask``
----------------------

``struct cpumask`` is the object that actually contains the cpumask bitmap
being queried, mutated, etc. A ``struct bpf_cpumask`` wraps a ``struct
cpumask``, which is why it's safe to cast it as such (note however that it is
**not** safe to cast a ``struct cpumask *`` to a ``struct bpf_cpumask *``, and
the verifier will reject any program that tries to do so).

As we'll see below, any kfunc that mutates its cpumask argument will take a
``struct bpf_cpumask *`` as that argument. Any argument that simply queries the
cpumask will instead take a ``struct cpumask *``.

3. cpumask kfuncs
=================

Above, we described the kfuncs that can be used to allocate, acquire, release,
etc a ``struct bpf_cpumask *``. This section of the document will describe the
kfuncs for mutating and querying cpumasks.

3.1 Mutating cpumasks
---------------------

Some cpumask kfuncs are "read-only" in that they don't mutate any of their
arguments, whereas others mutate at least one argument (which means that the
argument must be a ``struct bpf_cpumask *``, as described above).

This section will describe all of the cpumask kfuncs which mutate at least one
argument. :ref:`cpumasks-querying-label` below describes the read-only kfuncs.

3.1.1 Setting and clearing CPUs
-------------------------------

bpf_cpumask_set_cpu() and bpf_cpumask_clear_cpu() can be used to set and clear
a CPU in a ``struct bpf_cpumask`` respectively:

.. kernel-doc:: kernel/bpf/cpumask.c
   :identifiers: bpf_cpumask_set_cpu bpf_cpumask_clear_cpu

These kfuncs are pretty straightforward, and can be used, for example, as
follows:

.. code-block:: c

        /**
         * A sample tracepoint showing how a cpumask can be queried.
         */
        SEC("tp_btf/task_newtask")
        int BPF_PROG(test_set_clear_cpu, struct task_struct *task, u64 clone_flags)
        {
                struct bpf_cpumask *cpumask;

                cpumask = bpf_cpumask_create();
                if (!cpumask)
                        return -ENOMEM;

                bpf_cpumask_set_cpu(0, cpumask);
                if (!bpf_cpumask_test_cpu(0, cast(cpumask)))
                        /* Should never happen. */
                        goto release_exit;

                bpf_cpumask_clear_cpu(0, cpumask);
                if (bpf_cpumask_test_cpu(0, cast(cpumask)))
                        /* Should never happen. */
                        goto release_exit;

                /* struct cpumask * pointers such as task->cpus_ptr can also be queried. */
                if (bpf_cpumask_test_cpu(0, task->cpus_ptr))
                        bpf_printk("task %s can use CPU %d", task->comm, 0);

        release_exit:
                bpf_cpumask_release(cpumask);
                return 0;
        }

----

bpf_cpumask_test_and_set_cpu() and bpf_cpumask_test_and_clear_cpu() are
complementary kfuncs that allow callers to atomically test and set (or clear)
CPUs:

.. kernel-doc:: kernel/bpf/cpumask.c
   :identifiers: bpf_cpumask_test_and_set_cpu bpf_cpumask_test_and_clear_cpu

----

We can also set and clear entire ``struct bpf_cpumask *`` objects in one
operation using bpf_cpumask_setall() and bpf_cpumask_clear():

.. kernel-doc:: kernel/bpf/cpumask.c
   :identifiers: bpf_cpumask_setall bpf_cpumask_clear

3.1.2 Operations between cpumasks
---------------------------------

In addition to setting and clearing individual CPUs in a single cpumask,
callers can also perform bitwise operations between multiple cpumasks using
bpf_cpumask_and(), bpf_cpumask_or(), and bpf_cpumask_xor():

.. kernel-doc:: kernel/bpf/cpumask.c
   :identifiers: bpf_cpumask_and bpf_cpumask_or bpf_cpumask_xor

The following is an example of how they may be used. Note that some of the
kfuncs shown in this example will be covered in more detail below.

.. code-block:: c

        /**
         * A sample tracepoint showing how a cpumask can be mutated using
           bitwise operators (and queried).
         */
        SEC("tp_btf/task_newtask")
        int BPF_PROG(test_and_or_xor, struct task_struct *task, u64 clone_flags)
        {
                struct bpf_cpumask *mask1, *mask2, *dst1, *dst2;

                mask1 = bpf_cpumask_create();
                if (!mask1)
                        return -ENOMEM;

                mask2 = bpf_cpumask_create();
                if (!mask2) {
                        bpf_cpumask_release(mask1);
                        return -ENOMEM;
                }

                // ...Safely create the other two masks... */

                bpf_cpumask_set_cpu(0, mask1);
                bpf_cpumask_set_cpu(1, mask2);
                bpf_cpumask_and(dst1, (const struct cpumask *)mask1, (const struct cpumask *)mask2);
                if (!bpf_cpumask_empty((const struct cpumask *)dst1))
                        /* Should never happen. */
                        goto release_exit;

                bpf_cpumask_or(dst1, (const struct cpumask *)mask1, (const struct cpumask *)mask2);
                if (!bpf_cpumask_test_cpu(0, (const struct cpumask *)dst1))
                        /* Should never happen. */
                        goto release_exit;

                if (!bpf_cpumask_test_cpu(1, (const struct cpumask *)dst1))
                        /* Should never happen. */
                        goto release_exit;

                bpf_cpumask_xor(dst2, (const struct cpumask *)mask1, (const struct cpumask *)mask2);
                if (!bpf_cpumask_equal((const struct cpumask *)dst1,
                                       (const struct cpumask *)dst2))
                        /* Should never happen. */
                        goto release_exit;

         release_exit:
                bpf_cpumask_release(mask1);
                bpf_cpumask_release(mask2);
                bpf_cpumask_release(dst1);
                bpf_cpumask_release(dst2);
                return 0;
        }

----

The contents of an entire cpumask may be copied to another using
bpf_cpumask_copy():

.. kernel-doc:: kernel/bpf/cpumask.c
   :identifiers: bpf_cpumask_copy

----

.. _cpumasks-querying-label:

3.2 Querying cpumasks
---------------------

In addition to the above kfuncs, there is also a set of read-only kfuncs that
can be used to query the contents of cpumasks.

.. kernel-doc:: kernel/bpf/cpumask.c
   :identifiers: bpf_cpumask_first bpf_cpumask_first_zero bpf_cpumask_test_cpu

.. kernel-doc:: kernel/bpf/cpumask.c
   :identifiers: bpf_cpumask_equal bpf_cpumask_intersects bpf_cpumask_subset
                 bpf_cpumask_empty bpf_cpumask_full

.. kernel-doc:: kernel/bpf/cpumask.c
   :identifiers: bpf_cpumask_any bpf_cpumask_any_and

----

Some example usages of these querying kfuncs were shown above. We will not
replicate those exmaples here. Note, however, that all of the aforementioned
kfuncs are tested in `tools/testing/selftests/bpf/progs/cpumask_success.c`_, so
please take a look there if you're looking for more examples of how they can be
used.

.. _tools/testing/selftests/bpf/progs/cpumask_success.c:
   https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/tools/testing/selftests/bpf/progs/cpumask_success.c


4. Adding BPF cpumask kfuncs
============================

The set of supported BPF cpumask kfuncs are not (yet) a 1-1 match with the
cpumask operations in include/linux/cpumask.h. Any of those cpumask operations
could easily be encapsulated in a new kfunc if and when required. If you'd like
to support a new cpumask operation, please feel free to submit a patch. If you
do add a new cpumask kfunc, please document it here, and add any relevant
selftest testcases to the cpumask selftest suite.
