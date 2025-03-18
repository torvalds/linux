---
title: Checking your commit and tag signature verification status
intro: 'You can check the verification status of your commit and tag signatures on {% data variables.product.github %}.'
redirect_from:
  - /articles/checking-your-gpg-commit-and-tag-signature-verification-status
  - /articles/checking-your-commit-and-tag-signature-verification-status
  - /github/authenticating-to-github/checking-your-commit-and-tag-signature-verification-status
  - /github/authenticating-to-github/troubleshooting-commit-signature-verification/checking-your-commit-and-tag-signature-verification-status
versions:
  fpt: '*'
  ghes: '*'
  ghec: '*'
topics:
  - Identity
  - Access management
shortTitle: Check verification status
---

## Checking your commit signature verification status

1. On {% data variables.product.github %}, navigate to your pull request.
{% data reusables.repositories.review-pr-commits %}
1. Next to your commit's abbreviated commit hash, there is a box that shows whether your commit signature is verified{% ifversion fpt or ghec %}, partially verified,{% endif %} or unverified.

   ![Screenshot of a commit in the commit list for a repository. "Verified" is highlighted with an orange outline.](/assets/images/help/commits/verified-commit.png)
1. To view more detailed information about the commit signature, click **Verified**{% ifversion fpt or ghec %}, **Partially verified**,{% endif %} or **Unverified**.

   GPG signed commits will show the ID of the key that was used. SSH signed commits will show the signature of the public key that was used.

## Checking your tag signature verification status

{% data reusables.repositories.navigate-to-repo %}
{% data reusables.repositories.releases %}
1. At the top of the Releases page, click **Tags**.
1. Next to your tag description, there is a box that shows whether your tag signature is verified{% ifversion fpt or ghec %}, partially verified,{% endif %} or unverified.

   ![Screenshot of a tag in the tag list for a repository. "Verified" is highlighted with an orange outline.](/assets/images/help/commits/gpg-signed-tag-verified.png)
1. To view more detailed information about the tag signature, click **Verified**{% ifversion fpt or ghec %}, **Partially verified**,{% endif %} or **Unverified**.

## Further reading

* [AUTOTITLE](/authentication/managing-commit-signature-verification/about-commit-signature-verification)
* [AUTOTITLE](/authentication/managing-commit-signature-verification/signing-commits)
* [AUTOTITLE](/authentication/managing-commit-signature-verification/signing-tags)
