Submitting patches to bcachefs:
===============================

Patches must be tested before being submitted, either with the xfstests suite
[0], or the full bcachefs test suite in ktest [1], depending on what's being
touched. Note that ktest wraps xfstests and will be an easier method to running
it for most users; it includes single-command wrappers for all the mainstream
in-kernel local filesystems.

Patches will undergo more testing after being merged (including
lockdep/kasan/preempt/etc. variants), these are not generally required to be
run by the submitter - but do put some thought into what you're changing and
which tests might be relevant, e.g. are you dealing with tricky memory layout
work? kasan, are you doing locking work? then lockdep; and ktest includes
single-command variants for the debug build types you'll most likely need.

The exception to this rule is incomplete WIP/RFC patches: if you're working on
something nontrivial, it's encouraged to send out a WIP patch to let people
know what you're doing and make sure you're on the right track. Just make sure
it includes a brief note as to what's done and what's incomplete, to avoid
confusion.

Rigorous checkpatch.pl adherence is not required (many of its warnings are
considered out of date), but try not to deviate too much without reason.

Focus on writing code that reads well and is organized well; code should be
aesthetically pleasing.

CI:
===

Instead of running your tests locally, when running the full test suite it's
prefereable to let a server farm do it in parallel, and then have the results
in a nice test dashboard (which can tell you which failures are new, and
presents results in a git log view, avoiding the need for most bisecting).

That exists [2], and community members may request an account. If you work for
a big tech company, you'll need to help out with server costs to get access -
but the CI is not restricted to running bcachefs tests: it runs any ktest test
(which generally makes it easy to wrap other tests that can run in qemu).

Other things to think about:
============================

- How will we debug this code? Is there sufficient introspection to diagnose
  when something starts acting wonky on a user machine?

  We don't necessarily need every single field of every data structure visible
  with introspection, but having the important fields of all the core data
  types wired up makes debugging drastically easier - a bit of thoughtful
  foresight greatly reduces the need to have people build custom kernels with
  debug patches.

  More broadly, think about all the debug tooling that might be needed.

- Does it make the codebase more or less of a mess? Can we also try to do some
  organizing, too?

- Do new tests need to be written? New assertions? How do we know and verify
  that the code is correct, and what happens if something goes wrong?

  We don't yet have automated code coverage analysis or easy fault injection -
  but for now, pretend we did and ask what they might tell us.

  Assertions are hugely important, given that we don't yet have a systems
  language that can do ergonomic embedded correctness proofs. Hitting an assert
  in testing is much better than wandering off into undefined behaviour la-la
  land - use them. Use them judiciously, and not as a replacement for proper
  error handling, but use them.

- Does it need to be performance tested? Should we add new peformance counters?

  bcachefs has a set of persistent runtime counters which can be viewed with
  the 'bcachefs fs top' command; this should give users a basic idea of what
  their filesystem is currently doing. If you're doing a new feature or looking
  at old code, think if anything should be added.

- If it's a new on disk format feature - have upgrades and downgrades been
  tested? (Automated tests exists but aren't in the CI, due to the hassle of
  disk image management; coordinate to have them run.)

Mailing list, IRC:
==================

Patches should hit the list [3], but much discussion and code review happens on
IRC as well [4]; many people appreciate the more conversational approach and
quicker feedback.

Additionally, we have a lively user community doing excellent QA work, which
exists primarily on IRC. Please make use of that resource; user feedback is
important for any nontrivial feature, and documenting it in commit messages
would be a good idea.

[0]: git://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git
[1]: https://evilpiepirate.org/git/ktest.git/
[2]: https://evilpiepirate.org/~testdashboard/ci/
[3]: linux-bcachefs@vger.kernel.org
[4]: irc.oftc.net#bcache, #bcachefs-dev
