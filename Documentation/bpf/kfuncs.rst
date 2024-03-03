.. SPDX-License-Identifier: GPL-2.0

.. _kfuncs-header-label:

=============================
BPF Kernel Functions (kfuncs)
=============================

1. Introduction
===============

BPF Kernel Functions or more commonly known as kfuncs are functions in the Linux
kernel which are exposed for use by BPF programs. Unlike normal BPF helpers,
kfuncs do not have a stable interface and can change from one kernel release to
another. Hence, BPF programs need to be updated in response to changes in the
kernel. See :ref:`BPF_kfunc_lifecycle_expectations` for more information.

2. Defining a kfunc
===================

There are two ways to expose a kernel function to BPF programs, either make an
existing function in the kernel visible, or add a new wrapper for BPF. In both
cases, care must be taken that BPF program can only call such function in a
valid context. To enforce this, visibility of a kfunc can be per program type.

If you are not creating a BPF wrapper for existing kernel function, skip ahead
to :ref:`BPF_kfunc_nodef`.

2.1 Creating a wrapper kfunc
----------------------------

When defining a wrapper kfunc, the wrapper function should have extern linkage.
This prevents the compiler from optimizing away dead code, as this wrapper kfunc
is not invoked anywhere in the kernel itself. It is not necessary to provide a
prototype in a header for the wrapper kfunc.

An example is given below::

        /* Disables missing prototype warnings */
        __bpf_kfunc_start_defs();

        __bpf_kfunc struct task_struct *bpf_find_get_task_by_vpid(pid_t nr)
        {
                return find_get_task_by_vpid(nr);
        }

        __bpf_kfunc_end_defs();

A wrapper kfunc is often needed when we need to annotate parameters of the
kfunc. Otherwise one may directly make the kfunc visible to the BPF program by
registering it with the BPF subsystem. See :ref:`BPF_kfunc_nodef`.

2.2 Annotating kfunc parameters
-------------------------------

Similar to BPF helpers, there is sometime need for additional context required
by the verifier to make the usage of kernel functions safer and more useful.
Hence, we can annotate a parameter by suffixing the name of the argument of the
kfunc with a __tag, where tag may be one of the supported annotations.

2.2.1 __sz Annotation
---------------------

This annotation is used to indicate a memory and size pair in the argument list.
An example is given below::

        __bpf_kfunc void bpf_memzero(void *mem, int mem__sz)
        {
        ...
        }

Here, the verifier will treat first argument as a PTR_TO_MEM, and second
argument as its size. By default, without __sz annotation, the size of the type
of the pointer is used. Without __sz annotation, a kfunc cannot accept a void
pointer.

2.2.2 __k Annotation
--------------------

This annotation is only understood for scalar arguments, where it indicates that
the verifier must check the scalar argument to be a known constant, which does
not indicate a size parameter, and the value of the constant is relevant to the
safety of the program.

An example is given below::

        __bpf_kfunc void *bpf_obj_new(u32 local_type_id__k, ...)
        {
        ...
        }

Here, bpf_obj_new uses local_type_id argument to find out the size of that type
ID in program's BTF and return a sized pointer to it. Each type ID will have a
distinct size, hence it is crucial to treat each such call as distinct when
values don't match during verifier state pruning checks.

Hence, whenever a constant scalar argument is accepted by a kfunc which is not a
size parameter, and the value of the constant matters for program safety, __k
suffix should be used.

2.2.3 __uninit Annotation
-------------------------

This annotation is used to indicate that the argument will be treated as
uninitialized.

An example is given below::

        __bpf_kfunc int bpf_dynptr_from_skb(..., struct bpf_dynptr_kern *ptr__uninit)
        {
        ...
        }

Here, the dynptr will be treated as an uninitialized dynptr. Without this
annotation, the verifier will reject the program if the dynptr passed in is
not initialized.

2.2.4 __opt Annotation
-------------------------

This annotation is used to indicate that the buffer associated with an __sz or __szk
argument may be null. If the function is passed a nullptr in place of the buffer,
the verifier will not check that length is appropriate for the buffer. The kfunc is
responsible for checking if this buffer is null before using it.

An example is given below::

        __bpf_kfunc void *bpf_dynptr_slice(..., void *buffer__opt, u32 buffer__szk)
        {
        ...
        }

Here, the buffer may be null. If buffer is not null, it at least of size buffer_szk.
Either way, the returned buffer is either NULL, or of size buffer_szk. Without this
annotation, the verifier will reject the program if a null pointer is passed in with
a nonzero size.

2.2.5 __str Annotation
----------------------------
This annotation is used to indicate that the argument is a constant string.

An example is given below::

        __bpf_kfunc bpf_get_file_xattr(..., const char *name__str, ...)
        {
        ...
        }

In this case, ``bpf_get_file_xattr()`` can be called as::

        bpf_get_file_xattr(..., "xattr_name", ...);

Or::

        const char name[] = "xattr_name";  /* This need to be global */
        int BPF_PROG(...)
        {
                ...
                bpf_get_file_xattr(..., name, ...);
                ...
        }

.. _BPF_kfunc_nodef:

2.3 Using an existing kernel function
-------------------------------------

When an existing function in the kernel is fit for consumption by BPF programs,
it can be directly registered with the BPF subsystem. However, care must still
be taken to review the context in which it will be invoked by the BPF program
and whether it is safe to do so.

2.4 Annotating kfuncs
---------------------

In addition to kfuncs' arguments, verifier may need more information about the
type of kfunc(s) being registered with the BPF subsystem. To do so, we define
flags on a set of kfuncs as follows::

        BTF_KFUNCS_START(bpf_task_set)
        BTF_ID_FLAGS(func, bpf_get_task_pid, KF_ACQUIRE | KF_RET_NULL)
        BTF_ID_FLAGS(func, bpf_put_pid, KF_RELEASE)
        BTF_KFUNCS_END(bpf_task_set)

This set encodes the BTF ID of each kfunc listed above, and encodes the flags
along with it. Ofcourse, it is also allowed to specify no flags.

kfunc definitions should also always be annotated with the ``__bpf_kfunc``
macro. This prevents issues such as the compiler inlining the kfunc if it's a
static kernel function, or the function being elided in an LTO build as it's
not used in the rest of the kernel. Developers should not manually add
annotations to their kfunc to prevent these issues. If an annotation is
required to prevent such an issue with your kfunc, it is a bug and should be
added to the definition of the macro so that other kfuncs are similarly
protected. An example is given below::

        __bpf_kfunc struct task_struct *bpf_get_task_pid(s32 pid)
        {
        ...
        }

2.4.1 KF_ACQUIRE flag
---------------------

The KF_ACQUIRE flag is used to indicate that the kfunc returns a pointer to a
refcounted object. The verifier will then ensure that the pointer to the object
is eventually released using a release kfunc, or transferred to a map using a
referenced kptr (by invoking bpf_kptr_xchg). If not, the verifier fails the
loading of the BPF program until no lingering references remain in all possible
explored states of the program.

2.4.2 KF_RET_NULL flag
----------------------

The KF_RET_NULL flag is used to indicate that the pointer returned by the kfunc
may be NULL. Hence, it forces the user to do a NULL check on the pointer
returned from the kfunc before making use of it (dereferencing or passing to
another helper). This flag is often used in pairing with KF_ACQUIRE flag, but
both are orthogonal to each other.

2.4.3 KF_RELEASE flag
---------------------

The KF_RELEASE flag is used to indicate that the kfunc releases the pointer
passed in to it. There can be only one referenced pointer that can be passed
in. All copies of the pointer being released are invalidated as a result of
invoking kfunc with this flag. KF_RELEASE kfuncs automatically receive the
protection afforded by the KF_TRUSTED_ARGS flag described below.

2.4.4 KF_TRUSTED_ARGS flag
--------------------------

The KF_TRUSTED_ARGS flag is used for kfuncs taking pointer arguments. It
indicates that the all pointer arguments are valid, and that all pointers to
BTF objects have been passed in their unmodified form (that is, at a zero
offset, and without having been obtained from walking another pointer, with one
exception described below).

There are two types of pointers to kernel objects which are considered "valid":

1. Pointers which are passed as tracepoint or struct_ops callback arguments.
2. Pointers which were returned from a KF_ACQUIRE kfunc.

Pointers to non-BTF objects (e.g. scalar pointers) may also be passed to
KF_TRUSTED_ARGS kfuncs, and may have a non-zero offset.

The definition of "valid" pointers is subject to change at any time, and has
absolutely no ABI stability guarantees.

As mentioned above, a nested pointer obtained from walking a trusted pointer is
no longer trusted, with one exception. If a struct type has a field that is
guaranteed to be valid (trusted or rcu, as in KF_RCU description below) as long
as its parent pointer is valid, the following macros can be used to express
that to the verifier:

* ``BTF_TYPE_SAFE_TRUSTED``
* ``BTF_TYPE_SAFE_RCU``
* ``BTF_TYPE_SAFE_RCU_OR_NULL``

For example,

.. code-block:: c

	BTF_TYPE_SAFE_TRUSTED(struct socket) {
		struct sock *sk;
	};

or

.. code-block:: c

	BTF_TYPE_SAFE_RCU(struct task_struct) {
		const cpumask_t *cpus_ptr;
		struct css_set __rcu *cgroups;
		struct task_struct __rcu *real_parent;
		struct task_struct *group_leader;
	};

In other words, you must:

1. Wrap the valid pointer type in a ``BTF_TYPE_SAFE_*`` macro.

2. Specify the type and name of the valid nested field. This field must match
   the field in the original type definition exactly.

A new type declared by a ``BTF_TYPE_SAFE_*`` macro also needs to be emitted so
that it appears in BTF. For example, ``BTF_TYPE_SAFE_TRUSTED(struct socket)``
is emitted in the ``type_is_trusted()`` function as follows:

.. code-block:: c

	BTF_TYPE_EMIT(BTF_TYPE_SAFE_TRUSTED(struct socket));


2.4.5 KF_SLEEPABLE flag
-----------------------

The KF_SLEEPABLE flag is used for kfuncs that may sleep. Such kfuncs can only
be called by sleepable BPF programs (BPF_F_SLEEPABLE).

2.4.6 KF_DESTRUCTIVE flag
--------------------------

The KF_DESTRUCTIVE flag is used to indicate functions calling which is
destructive to the system. For example such a call can result in system
rebooting or panicking. Due to this additional restrictions apply to these
calls. At the moment they only require CAP_SYS_BOOT capability, but more can be
added later.

2.4.7 KF_RCU flag
-----------------

The KF_RCU flag is a weaker version of KF_TRUSTED_ARGS. The kfuncs marked with
KF_RCU expect either PTR_TRUSTED or MEM_RCU arguments. The verifier guarantees
that the objects are valid and there is no use-after-free. The pointers are not
NULL, but the object's refcount could have reached zero. The kfuncs need to
consider doing refcnt != 0 check, especially when returning a KF_ACQUIRE
pointer. Note as well that a KF_ACQUIRE kfunc that is KF_RCU should very likely
also be KF_RET_NULL.

.. _KF_deprecated_flag:

2.4.8 KF_DEPRECATED flag
------------------------

The KF_DEPRECATED flag is used for kfuncs which are scheduled to be
changed or removed in a subsequent kernel release. A kfunc that is
marked with KF_DEPRECATED should also have any relevant information
captured in its kernel doc. Such information typically includes the
kfunc's expected remaining lifespan, a recommendation for new
functionality that can replace it if any is available, and possibly a
rationale for why it is being removed.

Note that while on some occasions, a KF_DEPRECATED kfunc may continue to be
supported and have its KF_DEPRECATED flag removed, it is likely to be far more
difficult to remove a KF_DEPRECATED flag after it's been added than it is to
prevent it from being added in the first place. As described in
:ref:`BPF_kfunc_lifecycle_expectations`, users that rely on specific kfuncs are
encouraged to make their use-cases known as early as possible, and participate
in upstream discussions regarding whether to keep, change, deprecate, or remove
those kfuncs if and when such discussions occur.

2.5 Registering the kfuncs
--------------------------

Once the kfunc is prepared for use, the final step to making it visible is
registering it with the BPF subsystem. Registration is done per BPF program
type. An example is shown below::

        BTF_KFUNCS_START(bpf_task_set)
        BTF_ID_FLAGS(func, bpf_get_task_pid, KF_ACQUIRE | KF_RET_NULL)
        BTF_ID_FLAGS(func, bpf_put_pid, KF_RELEASE)
        BTF_KFUNCS_END(bpf_task_set)

        static const struct btf_kfunc_id_set bpf_task_kfunc_set = {
                .owner = THIS_MODULE,
                .set   = &bpf_task_set,
        };

        static int init_subsystem(void)
        {
                return register_btf_kfunc_id_set(BPF_PROG_TYPE_TRACING, &bpf_task_kfunc_set);
        }
        late_initcall(init_subsystem);

2.6  Specifying no-cast aliases with ___init
--------------------------------------------

The verifier will always enforce that the BTF type of a pointer passed to a
kfunc by a BPF program, matches the type of pointer specified in the kfunc
definition. The verifier, does, however, allow types that are equivalent
according to the C standard to be passed to the same kfunc arg, even if their
BTF_IDs differ.

For example, for the following type definition:

.. code-block:: c

	struct bpf_cpumask {
		cpumask_t cpumask;
		refcount_t usage;
	};

The verifier would allow a ``struct bpf_cpumask *`` to be passed to a kfunc
taking a ``cpumask_t *`` (which is a typedef of ``struct cpumask *``). For
instance, both ``struct cpumask *`` and ``struct bpf_cpmuask *`` can be passed
to bpf_cpumask_test_cpu().

In some cases, this type-aliasing behavior is not desired. ``struct
nf_conn___init`` is one such example:

.. code-block:: c

	struct nf_conn___init {
		struct nf_conn ct;
	};

The C standard would consider these types to be equivalent, but it would not
always be safe to pass either type to a trusted kfunc. ``struct
nf_conn___init`` represents an allocated ``struct nf_conn`` object that has
*not yet been initialized*, so it would therefore be unsafe to pass a ``struct
nf_conn___init *`` to a kfunc that's expecting a fully initialized ``struct
nf_conn *`` (e.g. ``bpf_ct_change_timeout()``).

In order to accommodate such requirements, the verifier will enforce strict
PTR_TO_BTF_ID type matching if two types have the exact same name, with one
being suffixed with ``___init``.

.. _BPF_kfunc_lifecycle_expectations:

3. kfunc lifecycle expectations
===============================

kfuncs provide a kernel <-> kernel API, and thus are not bound by any of the
strict stability restrictions associated with kernel <-> user UAPIs. This means
they can be thought of as similar to EXPORT_SYMBOL_GPL, and can therefore be
modified or removed by a maintainer of the subsystem they're defined in when
it's deemed necessary.

Like any other change to the kernel, maintainers will not change or remove a
kfunc without having a reasonable justification.  Whether or not they'll choose
to change a kfunc will ultimately depend on a variety of factors, such as how
widely used the kfunc is, how long the kfunc has been in the kernel, whether an
alternative kfunc exists, what the norm is in terms of stability for the
subsystem in question, and of course what the technical cost is of continuing
to support the kfunc.

There are several implications of this:

a) kfuncs that are widely used or have been in the kernel for a long time will
   be more difficult to justify being changed or removed by a maintainer. In
   other words, kfuncs that are known to have a lot of users and provide
   significant value provide stronger incentives for maintainers to invest the
   time and complexity in supporting them. It is therefore important for
   developers that are using kfuncs in their BPF programs to communicate and
   explain how and why those kfuncs are being used, and to participate in
   discussions regarding those kfuncs when they occur upstream.

b) Unlike regular kernel symbols marked with EXPORT_SYMBOL_GPL, BPF programs
   that call kfuncs are generally not part of the kernel tree. This means that
   refactoring cannot typically change callers in-place when a kfunc changes,
   as is done for e.g. an upstreamed driver being updated in place when a
   kernel symbol is changed.

   Unlike with regular kernel symbols, this is expected behavior for BPF
   symbols, and out-of-tree BPF programs that use kfuncs should be considered
   relevant to discussions and decisions around modifying and removing those
   kfuncs. The BPF community will take an active role in participating in
   upstream discussions when necessary to ensure that the perspectives of such
   users are taken into account.

c) A kfunc will never have any hard stability guarantees. BPF APIs cannot and
   will not ever hard-block a change in the kernel purely for stability
   reasons. That being said, kfuncs are features that are meant to solve
   problems and provide value to users. The decision of whether to change or
   remove a kfunc is a multivariate technical decision that is made on a
   case-by-case basis, and which is informed by data points such as those
   mentioned above. It is expected that a kfunc being removed or changed with
   no warning will not be a common occurrence or take place without sound
   justification, but it is a possibility that must be accepted if one is to
   use kfuncs.

3.1 kfunc deprecation
---------------------

As described above, while sometimes a maintainer may find that a kfunc must be
changed or removed immediately to accommodate some changes in their subsystem,
usually kfuncs will be able to accommodate a longer and more measured
deprecation process. For example, if a new kfunc comes along which provides
superior functionality to an existing kfunc, the existing kfunc may be
deprecated for some period of time to allow users to migrate their BPF programs
to use the new one. Or, if a kfunc has no known users, a decision may be made
to remove the kfunc (without providing an alternative API) after some
deprecation period so as to provide users with a window to notify the kfunc
maintainer if it turns out that the kfunc is actually being used.

It's expected that the common case will be that kfuncs will go through a
deprecation period rather than being changed or removed without warning. As
described in :ref:`KF_deprecated_flag`, the kfunc framework provides the
KF_DEPRECATED flag to kfunc developers to signal to users that a kfunc has been
deprecated. Once a kfunc has been marked with KF_DEPRECATED, the following
procedure is followed for removal:

1. Any relevant information for deprecated kfuncs is documented in the kfunc's
   kernel docs. This documentation will typically include the kfunc's expected
   remaining lifespan, a recommendation for new functionality that can replace
   the usage of the deprecated function (or an explanation as to why no such
   replacement exists), etc.

2. The deprecated kfunc is kept in the kernel for some period of time after it
   was first marked as deprecated. This time period will be chosen on a
   case-by-case basis, and will typically depend on how widespread the use of
   the kfunc is, how long it has been in the kernel, and how hard it is to move
   to alternatives. This deprecation time period is "best effort", and as
   described :ref:`above<BPF_kfunc_lifecycle_expectations>`, circumstances may
   sometimes dictate that the kfunc be removed before the full intended
   deprecation period has elapsed.

3. After the deprecation period the kfunc will be removed. At this point, BPF
   programs calling the kfunc will be rejected by the verifier.

4. Core kfuncs
==============

The BPF subsystem provides a number of "core" kfuncs that are potentially
applicable to a wide variety of different possible use cases and programs.
Those kfuncs are documented here.

4.1 struct task_struct * kfuncs
-------------------------------

There are a number of kfuncs that allow ``struct task_struct *`` objects to be
used as kptrs:

.. kernel-doc:: kernel/bpf/helpers.c
   :identifiers: bpf_task_acquire bpf_task_release

These kfuncs are useful when you want to acquire or release a reference to a
``struct task_struct *`` that was passed as e.g. a tracepoint arg, or a
struct_ops callback arg. For example:

.. code-block:: c

	/**
	 * A trivial example tracepoint program that shows how to
	 * acquire and release a struct task_struct * pointer.
	 */
	SEC("tp_btf/task_newtask")
	int BPF_PROG(task_acquire_release_example, struct task_struct *task, u64 clone_flags)
	{
		struct task_struct *acquired;

		acquired = bpf_task_acquire(task);
		if (acquired)
			/*
			 * In a typical program you'd do something like store
			 * the task in a map, and the map will automatically
			 * release it later. Here, we release it manually.
			 */
			bpf_task_release(acquired);
		return 0;
	}


References acquired on ``struct task_struct *`` objects are RCU protected.
Therefore, when in an RCU read region, you can obtain a pointer to a task
embedded in a map value without having to acquire a reference:

.. code-block:: c

	#define private(name) SEC(".data." #name) __hidden __attribute__((aligned(8)))
	private(TASK) static struct task_struct *global;

	/**
	 * A trivial example showing how to access a task stored
	 * in a map using RCU.
	 */
	SEC("tp_btf/task_newtask")
	int BPF_PROG(task_rcu_read_example, struct task_struct *task, u64 clone_flags)
	{
		struct task_struct *local_copy;

		bpf_rcu_read_lock();
		local_copy = global;
		if (local_copy)
			/*
			 * We could also pass local_copy to kfuncs or helper functions here,
			 * as we're guaranteed that local_copy will be valid until we exit
			 * the RCU read region below.
			 */
			bpf_printk("Global task %s is valid", local_copy->comm);
		else
			bpf_printk("No global task found");
		bpf_rcu_read_unlock();

		/* At this point we can no longer reference local_copy. */

		return 0;
	}

----

A BPF program can also look up a task from a pid. This can be useful if the
caller doesn't have a trusted pointer to a ``struct task_struct *`` object that
it can acquire a reference on with bpf_task_acquire().

.. kernel-doc:: kernel/bpf/helpers.c
   :identifiers: bpf_task_from_pid

Here is an example of it being used:

.. code-block:: c

	SEC("tp_btf/task_newtask")
	int BPF_PROG(task_get_pid_example, struct task_struct *task, u64 clone_flags)
	{
		struct task_struct *lookup;

		lookup = bpf_task_from_pid(task->pid);
		if (!lookup)
			/* A task should always be found, as %task is a tracepoint arg. */
			return -ENOENT;

		if (lookup->pid != task->pid) {
			/* bpf_task_from_pid() looks up the task via its
			 * globally-unique pid from the init_pid_ns. Thus,
			 * the pid of the lookup task should always be the
			 * same as the input task.
			 */
			bpf_task_release(lookup);
			return -EINVAL;
		}

		/* bpf_task_from_pid() returns an acquired reference,
		 * so it must be dropped before returning from the
		 * tracepoint handler.
		 */
		bpf_task_release(lookup);
		return 0;
	}

4.2 struct cgroup * kfuncs
--------------------------

``struct cgroup *`` objects also have acquire and release functions:

.. kernel-doc:: kernel/bpf/helpers.c
   :identifiers: bpf_cgroup_acquire bpf_cgroup_release

These kfuncs are used in exactly the same manner as bpf_task_acquire() and
bpf_task_release() respectively, so we won't provide examples for them.

----

Other kfuncs available for interacting with ``struct cgroup *`` objects are
bpf_cgroup_ancestor() and bpf_cgroup_from_id(), allowing callers to access
the ancestor of a cgroup and find a cgroup by its ID, respectively. Both
return a cgroup kptr.

.. kernel-doc:: kernel/bpf/helpers.c
   :identifiers: bpf_cgroup_ancestor

.. kernel-doc:: kernel/bpf/helpers.c
   :identifiers: bpf_cgroup_from_id

Eventually, BPF should be updated to allow this to happen with a normal memory
load in the program itself. This is currently not possible without more work in
the verifier. bpf_cgroup_ancestor() can be used as follows:

.. code-block:: c

	/**
	 * Simple tracepoint example that illustrates how a cgroup's
	 * ancestor can be accessed using bpf_cgroup_ancestor().
	 */
	SEC("tp_btf/cgroup_mkdir")
	int BPF_PROG(cgrp_ancestor_example, struct cgroup *cgrp, const char *path)
	{
		struct cgroup *parent;

		/* The parent cgroup resides at the level before the current cgroup's level. */
		parent = bpf_cgroup_ancestor(cgrp, cgrp->level - 1);
		if (!parent)
			return -ENOENT;

		bpf_printk("Parent id is %d", parent->self.id);

		/* Return the parent cgroup that was acquired above. */
		bpf_cgroup_release(parent);
		return 0;
	}

4.3 struct cpumask * kfuncs
---------------------------

BPF provides a set of kfuncs that can be used to query, allocate, mutate, and
destroy struct cpumask * objects. Please refer to :ref:`cpumasks-header-label`
for more details.
