.. _configuregit:

Configure Git
=============

This chapter describes maintainer level git configuration.

Tagged branches used in :ref:`Documentation/maintainer/pull-requests.rst
<pullrequests>` should be signed with the developers public GPG key. Signed
tags can be created by passing the ``-u`` flag to ``git tag``. However,
since you would *usually* use the same key for the same project, you can
set it once with
::

	git config user.signingkey "keyname"

Alternatively, edit your ``.git/config`` or ``~/.gitconfig`` file by hand:
::

	[user]
		name = Jane Developer
		email = jd@domain.org
		signingkey = jd@domain.org

You may need to tell ``git`` to use ``gpg2``
::

	[gpg]
		program = /path/to/gpg2

You may also like to tell ``gpg`` which ``tty`` to use (add to your shell rc file)
::

	export GPG_TTY=$(tty)
