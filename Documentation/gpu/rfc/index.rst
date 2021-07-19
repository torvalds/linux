===============
GPU RFC Section
===============

For complex work, especially new uapi, it is often good to nail the high level
design issues before getting lost in the code details. This section is meant to
host such documentation:

* Each RFC should be a section in this file, explaining the goal and main design
  considerations. Especially for uapi make sure you Cc: all relevant project
  mailing lists and involved people outside of dri-devel.

* For uapi structures add a file to this directory with and then pull the
  kerneldoc in like with real uapi headers.

* Once the code has landed move all the documentation to the right places in
  the main core, helper or driver sections.
