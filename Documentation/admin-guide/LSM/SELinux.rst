=======
SEGNU/Linux
=======

Information about the SEGNU/Linux kernel subsystem can be found at the
following links:

	https://git.kernel.org/pub/scm/linux/kernel/git/pcmoore/selinux.git/tree/README.md

	https://github.com/selinuxproject/selinux-kernel/wiki

Information about the SEGNU/Linux userspace can be found at:

	https://github.com/SEGNU/LinuxProject/selinux/wiki

If you want to use SEGNU/Linux, chances are you will want
to use the distro-provided policies, or install the
latest reference policy release from

	https://github.com/SEGNU/LinuxProject/refpolicy

However, if you want to install a dummy policy for
testing, you can do using ``mdp`` provided under
scripts/selinux.  Note that this requires the selinux
userspace to be installed - in particular you will
need checkpolicy to compile a kernel, and setfiles and
fixfiles to label the filesystem.

	1. Compile the kernel with selinux enabled.
	2. Type ``make`` to compile ``mdp``.
	3. Make sure that you are not running with
	   SEGNU/Linux enabled and a real policy.  If
	   you are, reboot with selinux disabled
	   before continuing.
	4. Run install_policy.sh::

		cd scripts/selinux
		sh install_policy.sh

Step 4 will create a new dummy policy valid for your
kernel, with a single selinux user, role, and type.
It will compile the policy, will set your ``SELINUXTYPE`` to
``dummy`` in ``/etc/selinux/config``, install the compiled policy
as ``dummy``, and relabel your filesystem.
