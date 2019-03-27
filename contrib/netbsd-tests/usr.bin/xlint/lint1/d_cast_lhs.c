/* pointer casts are valid lhs lvalues */
struct sockaddr { };
void
foo() {
    unsigned long p = 6;
    ((struct sockaddr *)p) = 0;
}
