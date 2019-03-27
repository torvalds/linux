This directory contains sample code for using BearSSL.

client_basic.c

   A sample client code, that connects to a server, performs a SSL
   handshake, sends a basic HTTP GET request, and dumps the complete
   answer on stdout.

   Compile it against BearSSL headers (in the ../inc directory) and
   library (libbearssl.a). This code will validate the server
   certificate against two hardcoded trust anchors.

server_basic.c

   A sample SSL server, that serves one client at a time. It reads a
   single HTTP request (that it does not really parse; it just waits for
   the two successive line endings that mark the end of the request),
   and pushes a basic response.

   Compile it against BearSSL headers (in the ../inc directory) and
   library (libbearssl.a). Depending on compilation options (see the
   code), it will use one of several certificate chains, that exercise
   various combinations of RSA and EC keys and signatures. These
   certificate chains link to the trust anchors that are hardcoded
   in client_basic.c, so the sample client and the sample server can
   be tested against each other.

custom_profile.c

   A sample C source file that shows how to write your own client or
   server profiles (selections of cipher suites and algorithms).


The .pem files are certificate and keys corresponding to the chains
and anchors used by the sample client and server. They are provided
for reference only; these files are not used by the examples.
