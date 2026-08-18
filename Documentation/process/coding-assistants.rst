.. SPDX-License-Identifier: GPL-2.0

.. _coding_assistants:

AI Coding Assistants
++++++++++++++++++++

This document provides guidance for AI tools and developers using AI
assistance when contributing to the Linux kernel.

AI tools helping with Linux kernel development should follow the standard
kernel development process:

* Documentation/process/development-process.rst
* Documentation/process/coding-style.rst
* Documentation/process/submitting-patches.rst

Licensing and Legal Requirements
================================

All contributions must comply with the kernel's licensing requirements:

* All code must be compatible with GPL-2.0-only
* Use appropriate SPDX license identifiers
* See Documentation/process/license-rules.rst for details

Signed-off-by and Developer Certificate of Origin
=================================================

AI agents MUST NOT add Signed-off-by tags. Only humans can legally
certify the Developer Certificate of Origin (DCO). The human submitter
is responsible for:

* Reviewing all AI-generated code
* Ensuring compliance with licensing requirements
* Adding their own Signed-off-by tag to certify the DCO
* Taking full responsibility for the contribution

Attribution
===========

When AI tools contribute to kernel development, proper attribution
helps track the evolving role of AI in the development process.
Contributions should include an Assisted-by tag in the following format::

  Assisted-by: AGENT_NAME:MODEL_VERSION [TOOL1] [TOOL2]

Where:

* ``AGENT_NAME`` is the name of the AI tool or framework
* ``MODEL_VERSION`` is the specific model version used
* ``[TOOL1] [TOOL2]`` are optional specialized analysis tools used
  (e.g., coccinelle, sparse, smatch, clang-tidy)

Basic development tools (git, gcc, make, editors) should not be listed.

Example::

  Assisted-by: Claude:claude-3-opus coccinelle sparse

Procedure for finding and fixing bugs
=====================================

When an AI assistant is used to find and fix bugs, it **MUST** follow at least
these steps:

1. Before starting, read the whole process documentation listed above, as well
   as any other document mentioned in the request. Do not rely on isolated
   parts found by keyword search.
2. Note the commit ID and Locate a bug as instructed.
3. For any bug found that is not trivial, verify that it looks real by
   attempting to create a reproducer to demonstrate it. Lacking it may cause
   the report to be ignored, as many unverified bug reports sent to maintainers
   happen to be invalid. Stop here if it finally looks wrong.
4. Write a fix for the bug. This part is not optional: except in a few very
   rare cases, an AI assistant able to find a bug is able to fix it. Note that
   fixes written in the same session as used to find the bug will generally
   lead to better and more accurate fixes as the LLM's reasoning context
   remains present.
5. Build and verify that the fix works either using the reproducer or by
   re-running a complete analysis; drop any fix that doesn't work and try
   another one. The fix must not add build warnings and must pass the
   checkpatch.pl checks (see submitting-patches.rst).
6. Commit the working fix with a detailed message describing the problem, the
   solution and a Fixes tag. Do not add a Signed-off-by tag, and add an
   Assisted-by tag, as described above.
7. Identify the maintainers and lists using scripts/get_maintainer.pl.
   Documentation/process/security-bugs.rst shows how to do that.
8. Indicate what could not be done. If the fix could not be built or tested, or
   if no reproducer could be produced, say so explicitly: maintainers currently
   waste too much time analyzing unverified reports and untested fixes.
9. Read Documentation/process/threat-model.rst to determine whether the bug is
   a vulnerability or a regular bug, and leave the result to the reporter for
   review (the assistant must never send anything itself). Regular bugs are
   submitted as described in Documentation/process/submitting-patches.rst,
   vulnerabilities as described in Documentation/process/security-bugs.rst.
