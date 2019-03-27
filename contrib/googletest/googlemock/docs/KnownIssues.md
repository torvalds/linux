As any non-trivial software system, Google Mock has some known limitations and problems.  We are working on improving it, and welcome your help!  The follow is a list of issues we know about.



## README contains outdated information on Google Mock's compatibility with other testing frameworks ##

The `README` file in release 1.1.0 still says that Google Mock only works with Google Test.  Actually, you can configure Google Mock to work with any testing framework you choose.

## Tests failing on machines using Power PC CPUs (e.g. some Macs) ##

`gmock_output_test` and `gmock-printers_test` are known to fail with Power PC CPUs.  This is due to portability issues with these tests, and is not an indication of problems in Google Mock itself.  You can safely ignore them.

## Failed to resolve libgtest.so.0 in tests when built against installed Google Test ##

This only applies if you manually built and installed Google Test, and then built a Google Mock against it (either explicitly, or because gtest-config was in your path post-install). In this situation, Libtool has a known issue with certain systems' ldconfig setup:

http://article.gmane.org/gmane.comp.sysutils.automake.general/9025

This requires a manual run of "sudo ldconfig" after the "sudo make install" for Google Test before any binaries which link against it can be executed. This isn't a bug in our install, but we should at least have documented it or hacked a work-around into our install. We should have one of these solutions in our next release.