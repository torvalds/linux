.. SPDX-License-Identifier: GPL-2.0

====
CVEs
====

Os números Common Vulnerabilities and Exposure (CVE®) foram desenvolvidos
como uma forma inequívoca de identificar, definir e catalogar vulnerabilidades
de segurança divulgadas publicamente. Com o tempo, sua utilidade diminuiu em
relação ao projeto do kernel, e os números CVE foram frequentemente atribuídos
de formas inadequadas e por motivos inadequados. Por causa disso, a comunidade
de desenvolvimento do kernel tendeu a evitá-los. No entanto, a combinação da
pressão contínua para atribuir CVEs e outras formas de identificadores de
segurança, e abusos contínuos por indivíduos e empresas de fora da comunidade
do kernel, deixou claro que a comunidade do kernel deve controlar
essas atribuições.

A equipe de desenvolvedores do kernel Linux tem a capacidade de atribuir CVEs
para possíveis problemas de segurança do kernel Linux. Essa atribuição é
independente do processo normal de relato de bugs de segurança do kernel
Linux, descrito em :ref:`securitybugs`.

Uma lista de todos os CVEs atribuídos ao kernel Linux pode ser encontrada nos
arquivos da lista de discussão linux-cve, como visto em
https://lore.kernel.org/linux-cve-announce/. Para receber notificações sobre
os CVEs atribuídos, por favor, `inscreva-se
<https://subspace.kernel.org/subscribing.html>`_ nessa lista de discussão.

Processo
========

Como parte do processo normal de lançamento estável, alterações do kernel que
são potencialmente problemas de segurança são identificadas pelos
desenvolvedores responsáveis pelas atribuições de números CVE e recebem
automaticamente números CVE. Essas atribuições são publicadas na lista de
discussão linux-cve-announce como anúncios frequentes.

Observe que, devido à camada em que o kernel Linux se encontra em um sistema,
quase qualquer bug pode ser explorável para comprometer a segurança do kernel,
mas a possibilidade de exploração muitas vezes não é evidente quando o bug é
corrigido. Por causa disso, a equipe de atribuição de CVEs é excessivamente
cautelosa e atribui números CVE a qualquer correção de bug que identificar.
Isso explica o número aparentemente grande de CVEs emitidos pela equipe do
kernel Linux.

Se a equipe de atribuição de CVEs deixar passar uma correção específica que
qualquer usuário considere que deveria receber um CVE, por favor envie um
e-mail para <cve@kernel.org> e a equipe trabalhará com você nisso. Observe
que nenhum possível problema de segurança deve ser enviado para esse alias;
ele é SOMENTE para atribuição de CVEs a correções que já estejam em árvores de
kernel lançadas. Se você acredita ter encontrado um problema de segurança
ainda
não corrigido, por favor siga o processo normal de relato de bugs de segurança do kernel
Linux, descrito em :ref:`securitybugs`.

Nenhum CVE será atribuído automaticamente para problemas de segurança ainda
não corrigidos no kernel Linux; a atribuição só acontecerá automaticamente
depois que uma correção estiver disponível e aplicada a uma árvore de kernel
estável, e ela será rastreada dessa forma pelo ID do commit git da correção
original. Se alguém desejar que um CVE seja atribuído antes que um problema
seja resolvido com um commit, por favor entre em contato com a equipe de
atribuição de CVEs do kernel em <cve@kernel.org> para obter um identificador
atribuído a partir de seu lote de identificadores reservados.

Nenhum CVE será atribuído para qualquer problema encontrado em uma versão do
kernel que atualmente não esteja sendo mantida ativamente pela equipe de kernel
Stable/LTS. Uma lista dos ramos de kernel atualmente suportados pode ser
encontrada em https://kernel.org/releases.html

Contestações de CVEs atribuídos
===============================

A autoridade para contestar ou modificar um CVE atribuído a uma alteração
específica do kernel pertence exclusivamente aos mantenedores do subsistema
relevante afetado. Esse princípio garante um alto grau de precisão e
responsabilização no relato de vulnerabilidades. Somente esses indivíduos, com
profundo conhecimento técnico e conhecimento íntimo do subsistema, podem
avaliar de forma eficaz a validade e o escopo de uma vulnerabilidade relatada e
determinar sua designação CVE apropriada. Qualquer tentativa de modificar ou
contestar um CVE fora dessa autoridade designada pode levar a confusão, relato
impreciso e, em última análise, sistemas comprometidos.

CVEs inválidos
==============

Se um problema de segurança for encontrado em um kernel Linux que é suportado
apenas por uma distribuição Linux devido às alterações feitas por essa
distribuição, ou porque a distribuição oferece suporte a uma versão do kernel
que não é mais uma das versões suportadas pelo kernel.org, então um CVE não
pode ser atribuído pela equipe de CVEs do kernel Linux e deve ser solicitado à
própria distribuição Linux.

Qualquer CVE atribuído ao kernel Linux para uma versão de kernel
ativamente suportada, por qualquer grupo que não seja a equipe de atribuição de
CVEs do kernel, não deve ser tratado como um CVE válido. Por favor, notifique
a equipe de atribuição de CVEs do kernel em <cve@kernel.org> para que ela
possa trabalhar para invalidar essas entradas por meio do processo de remediação
da CNA.

Aplicabilidade de CVEs específicos
==================================

Como o kernel Linux pode ser usado de múltiplas maneiras, com diversas
formas de acesso por usuários externos, ou sem nenhum acesso, a
aplicabilidade de qualquer CVE específico cabe ao usuário do Linux determinar;
isso não cabe à equipe de atribuição de CVEs. Por favor, não entre em contato
conosco para tentar determinar a aplicabilidade de qualquer CVE específico.

Além disso, como a árvore de códigos-fonte é muito grande, e cada sistema usa
apenas um pequeno subconjunto dessa árvore, o usuário do Linux deve estar
ciente de que grandes quantidades de CVEs atribuídos não são relevantes para
seus sistemas.

Em resumo, não conhecemos o seu caso de uso e não sabemos quais partes do
kernel você usa, portanto não há como determinarmos se um CVE específico é
relevante para o seu sistema.

Como sempre, o melhor é adotar todas as alterações de kernel lançadas, pois
elas são testadas em conjunto como um todo unificado por muitos membros da
comunidade, e não como alterações individuais selecionadas. Observe também que,
para muitos bugs, a solução do problema geral não é encontrada em uma única
alteração, mas pela soma de muitas correções umas sobre as outras. Idealmente,
CVEs serão atribuídos a todas as correções de todos os problemas, mas às vezes
podemos deixar de perceber algumas correções; portanto, presuma que algumas alterações
sem um CVE atribuído podem ser relevantes para adotar.
