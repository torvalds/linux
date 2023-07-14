Configuring Git
===============

This chapter describes maintainer level git configuration.

Tagged branches used in pull requests (see
Documentation/maintainer/pull-requests.rst) should be signed with the
developers public GPG key. Signed tags can be created by passing
``-u <key-id>`` to ``git tag``. However, since you would *usually* use the same
key for the project, you can set it in the configuration and use the ``-s``
flag. To set the default ``key-id`` use::

	git config user.signingkey "keyname"

Alternatively, edit your ``.git/config`` or ``~/.gitconfig`` file by hand::

	[user]
		name = Jane Developer
		email = jd@domain.org
		signingkey = jd@domain.org

You may need to tell ``git`` to use ``gpg2``::

	[gpg]
		program = /path/to/gpg2

You may also like to tell ``gpg`` which ``tty`` to use (add to your shell
rc file)::

	export GPG_TTY=$(tty)


Creating commit links to lore.kernel.org
----------------------------------------

The web site http://lore.kernel.org is meant as a grand archive of all mail
list traffic concerning or influencing the kernel development. Storing archives
of patches here is a recommended practice, and when a maintainer applies a
patch to a subsystem tree, it is a good idea to provide a Link: tag with a
reference back to the lore archive so that people that browse the commit
history can find related discussions and rationale behind a certain change.
The link tag will look like this::

    Link: https://lore.kernel.org/r/<message-id>

This can be configured to happen automatically any time you issue ``git am``
by adding the following hook into your git::

	$ git config am.messageid true
	$ cat >.git/hooks/applypatch-msg <<'EOF'
	#!/bin/sh
	. git-sh-setup
	perl -pi -e 's|^Message-I[dD]:\s*<?([^>]+)>?$|Link: https://lore.kernel.org/r/$1|g;' "$1"
	test -x "$GIT_DIR/hooks/commit-msg" &&
		exec "$GIT_DIR/hooks/commit-msg" ${1+"$@"}
	:
	EOF
	$ chmod a+x .git/hooks/applypatch-msg
