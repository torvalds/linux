=============
Collaboration
=============

Collaboration is essential in open source world and we encourage you
to pick a team partner to work on selected assignments.

Here is a simple guide to get you started:

1. Use Github / Gitlab
----------------------

Best way to share your work inside the team is to use a version control system (VCS)
in order to track each change. Mind that you must make your repo private and only allow
read/write access rights to team members.

2. Start with a skeleton for the assignment
-------------------------------------------

Add `init`/`exit` functions, driver operations and global structures that you driver might need.

.. code-block:: c

  // SPDX-License-Identifier: GPL-2.0
  /*
   * uart16550.c - UART16550 driver
   *
   * Author: John Doe <john.doe@mail.com>
   * Author: Ionut Popescu <ionut.popescu@mail.com>
   */
  struct uart16550_dev {
     struct cdev cdev;
     /*TODO */
  };

  static struct uart16550_dev devs[MAX_NUMBER_DEVICES];

  static int uart16550_open(struct inode *inode, struct file *file)
  {
      /*TODO */
      return 0;
  }

  static int uart16550_release(struct inode *inode, struct file *file)
  {
     /*TODO */
     return 0;
  }

  static ssize_t uart16550_read(struct file *file,  char __user *user_buffer,
                                size_t size, loff_t *offset)
  {
        /*TODO */
  }

  static ssize_t uart16550_write(struct file *file,
                                 const char __user *user_buffer,
                                 size_t size, loff_t *offset)
  {
       /*TODO */
  }

  static long
  uart16550_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
  {
        /*TODO */
        return 0;
  }

  static const struct file_operations uart16550_fops = {
         .owner		 = THIS_MODULE,
         .open		 = uart16550_open,
         .release	 = uart16550_release,
         .read		 = uart16550_read,
         .write		 = uart16550_write,
         .unlocked_ioctl = uart16550_ioctl
  };

  static int __init uart16550_init(void)
  {
    /* TODO: */
  }

  static void __exit uart16550_exit(void)
  {
     /* TODO: */
  }

  module_init(uart16550_init);
  module_exit(uart16550_exit);

  MODULE_DESCRIPTION("UART16550 Driver");
  MODULE_AUTHOR("John Doe <john.doe@mail.com");
  MODULE_AUTHOR("Ionut Popescu <ionut.popescu@mail.com");

3. Add a commit for each individual change
------------------------------------------

First commit must always be the skeleton file. And the rest of the code should be on top of skeleton file.
Please write a good commit mesage. Explain briefly what the commit does and *why* it is necessary.

Follow the seven rules of writing a good commit message: https://cbea.ms/git-commit/#seven-rules

.. code-block:: console

  Commit 3c92a02cc52700d2cd7c50a20297eef8553c207a (HEAD -> tema2)
  Author: John Doe <john.doe@mail.com>
  Date:   Mon Apr 4 11:54:39 2022 +0300

    uart16550: Add initial skeleton for ssignment #2

    This adds simple skeleton file for uart16550 assignment. Notice
    module init/exit callbacks and file_operations dummy implementation
    for open/release/read/write/ioctl.

    Signed-off-by: John Doe <john.doe@mail.com>

4. Split the work inside the team
---------------------------------

Add `TODOs` with each team member tasks. Try to split the work evenly.

Before starting to code, make a plan. On top of your skeleton file, add TODOs with each member tasks. Agree on global
structures and the overlall driver design. Then start coding.

5. Do reviews
-------------

Create Pull Requests with your commits and go through review rounds with your team members. You can follow `How to create a PR` `video <https://www.youtube.com/watch?v=YvoHJJWvn98>`_.

6. Merge the work
-----------------

The final work is the result of merging all the pull requests. Following the commit messages
one should clearly understand the progress of the code and how the work was managed inside the team.

.. code-block:: console

  f5118b873294 uart16550: Add uart16550_interrupt implementation
  2115503fc3e3 uart16550: Add uart16550_ioctl implementation
  b31a257fd8b8 uart16550: Add uart16550_write implementation
  ac1af6d88a25 uart16550: Add uart16550_read implementation
  9f680e8136bf uart16550: Add uart16550_open/release implementation
  3c92a02cc527 uart16550: Add skeleton for SO2 assignment #2
