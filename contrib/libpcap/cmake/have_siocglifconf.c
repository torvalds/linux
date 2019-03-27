#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
int main() {
    ioctl(0, SIOCGLIFCONF, (char *)0);
}
