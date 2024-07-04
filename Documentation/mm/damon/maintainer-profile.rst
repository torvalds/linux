.. SPDX-License-Identifier: GPL-2.0

DAMON Maintainer Entry Profile
==============================

The DAMON subsystem covers the files that are listed in 'DATA ACCESS MONITOR'
section of 'MAINTAINERS' file.

The mailing lists for the subsystem are damon@lists.linux.dev and
linux-mm@kvack.org.  Patches should be made against the mm-unstable tree [1]_
whenever possible and posted to the mailing lists.

SCM Trees
---------

There are multiple Linux trees for DAMON development.  Patches under
development or testing are queued in damon/next [2]_ by the DAMON maintainer.
Sufficiently reviewed patches will be queued in mm-unstable [1]_ by the memory
management subsystem maintainer.  After more sufficient tests, the patches will
be queued in mm-stable [3]_ , and finally pull-requested to the mainline by the
memory management subsystem maintainer.

Note again the patches for mm-unstable tree [1]_ are queued by the memory
management subsystem maintainer.  If the patches requires some patches in
damon/next tree [2]_ which not yet merged in mm-unstable, please make sure the
requirement is clearly specified.

Submit checklist addendum
-------------------------

When making DAMON changes, you should do below.

- Build changes related outputs including kernel and documents.
- Ensure the builds introduce no new errors or warnings.
- Run and ensure no new failures for DAMON selftests [4]_ and kunittests [5]_ .

Further doing below and putting the results will be helpful.

- Run damon-tests/corr [6]_ for normal changes.
- Run damon-tests/perf [7]_ for performance changes.

Key cycle dates
---------------

Patches can be sent anytime.  Key cycle dates of the mm-unstable [1]_ and
mm-stable [3]_ trees depend on the memory management subsystem maintainer.

Review cadence
--------------

The DAMON maintainer does the work on the usual work hour (09:00 to 17:00,
Mon-Fri) in PT (Pacific Time).  The response to patches will occasionally be
slow.  Do not hesitate to send a ping if you have not heard back within a week
of sending a patch.


.. [1] https://git.kernel.org/akpm/mm/h/mm-unstable
.. [2] https://git.kernel.org/sj/h/damon/next
.. [3] https://git.kernel.org/akpm/mm/h/mm-stable
.. [4] https://github.com/awslabs/damon-tests/blob/master/corr/run.sh#L49
.. [5] https://github.com/awslabs/damon-tests/blob/master/corr/tests/kunit.sh
.. [6] https://github.com/awslabs/damon-tests/tree/master/corr
.. [7] https://github.com/awslabs/damon-tests/tree/master/perf
